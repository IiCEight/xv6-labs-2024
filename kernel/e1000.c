#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"

/* Descriptor Queue Structure. Refer to [e1000 3.2.6 and 3.4] */
#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static char *tx_bufs[TX_RING_SIZE];

//
// NOTE:Head should point to the first valid receive descriptor in the
// descriptor ring and tail should point to one descriptor beyond the last
// valid descriptor in the descriptor ring.
//
#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static char *rx_bufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
//
// See [E1000 14.3] for more details.
//
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_bufs[i] = 0;
  }

  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_bufs[i] = kalloc();
    if (!rx_bufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_bufs[i];
  }

  // [E1000 3.2.6]
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  // NOTE: This register holds a value that is an offset from the base
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  // See [E1000 14.4]
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  // [e1000 14.5 page 377]
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  // The TX Inter-Packet Gap (IPG) is the mandatory idle time 
  // the MAC must leave on the Ethernet wire between one 
  // transmitted frame and the next.
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  // [e1000 14.4 page 377]
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  // interrupt after every received packet (no timer) [e1000 3.2.7.1.1]
  regs[E1000_RDTR] = 0; 
  // interrupt after every packet (no timer) [e1000 3.2.7.1.2]
  regs[E1000_RADV] = 0; 
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(char *buf, int len)
{
  //
  // Your code here.
  //
  // buf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after send completes.
  //
    printf("e1000_transmit: Begin.... length %d\n", len);

    if(len > PGSIZE)
    {
        printf("e1000_transmit: Invalid length %d\n", len);
        return -1;
    }
    acquire(&e1000_lock);
    struct tx_desc *txdp = &tx_ring[regs[E1000_TDT]];
    if((txdp->status & E1000_TXD_STAT_DD) == 0)
    {
        release(&e1000_lock);
        printf("e1000_transmit: Tail descriptor is not available.\n");
        return -1;
    }
    // free old buffer and point to a new one which needs to transmit.
    printf("Begin to free %ld\n", (uint64)txdp->addr);
    if(txdp->addr != 0)
        kfree((void *)txdp->addr);
    printf("End to free %ld\n", (uint64)txdp->addr);
    txdp->addr = (uint64)buf;
    txdp->length = len;
    txdp->cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
    regs[E1000_TDT] = (regs[E1000_TDT] + 1) % TX_RING_SIZE;
    release(&e1000_lock);
    return 0;
}

// See [E1000 3.2.6] for details about the receive process.
static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver a buf for each packet (using net_rx()).
  //
    while(1)
    {
        acquire(&e1000_lock);
        struct rx_desc *rxdp = &rx_ring[(regs[E1000_RDT] + 1) % RX_RING_SIZE];
        if((rxdp->status & E1000_RXD_STAT_DD) == 0)
        {
            release(&e1000_lock);
            printf("receive queue: head %d, tail %d\n", regs[E1000_RDH], regs[E1000_RDT]);
            printf("No packet is available\n");
            return;
        }
        char * recvbuf = (char *)rxdp->addr;
        int len = rxdp->length;
        uint64 mem = (uint64)kalloc();
        if(!mem)
        {
            release(&e1000_lock);
            panic("e1000_recv: Allocation of memory failed");
        }
        // allocate a new buffer for the descriptor and clear status
        rxdp->addr = mem;
        rxdp->status = 0;
        regs[E1000_RDT] = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
        release(&e1000_lock);
        // NOTE: net_rx will try to get *e1000_lock* if its ARP request.
        // So it must be placed out of critical region otherwise deadlock happens.
        net_rx(recvbuf, len);
    }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
