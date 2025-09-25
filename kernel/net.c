#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

// For space saving, I use an expandable array for these recvqueues 
// like vector<recv_queue> in C++ STL.
// And recvqueuesend marks its end. But find one will cost O(N).
static struct recv_queue recvqueues[MAX_PORT_NUM];
static int recvqueuesend = 0;

void
netinit(void)
{
  initlock(&netlock, "netlock");
}

//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  //
  // Your code here.
  //
    int port;
    argint(0, &port);
    acquire(&netlock);
    if(recvqueuesend >= MAX_PORT_NUM)
    {
        release(&netlock);
        printf("bind: Exceed maximum number of binded ports.\n");
        return -1;
    }
    recvqueues[recvqueuesend++] = (struct recv_queue){port, {0}, 0, 0, 0};
    release(&netlock);
    return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //

  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  //
  // Your code here.
  //
    int dport;
    uint64 srcaddr;
    uint64 sportaddr;
    uint64 bufaddr; 
    int maxlen;
    argint(0, &dport);
    argaddr(1, &srcaddr);
    argaddr(2, &sportaddr);
    argaddr(3, &bufaddr);
    argint(4, &maxlen);
    
    acquire(&netlock);
    // Find recv queue with corresponding port.
    int found = -1;
    for(int i = 0; i < recvqueuesend; i++)
    {
        if(recvqueues[i].port == dport)
        {
            found = i;
            break;
        }
    }
    if(found == -1)
    {
        release(&netlock);
        printf("recv: No bind port %d\n", dport);
        return -1;
    }

    // Since wakeup will wake all the sleepers on the chan,
    // so there may be multiple processes waiting on the same port.
    // But only one process can get one packet each time.
    // So we need to check the queue size in a while loop.
    while(recvqueues[found].size == 0)
    {
        // sleep will auto release netlock and reacquire it when wakeup
        sleep(&recvqueues[found], &netlock);
    }

    // assert recvqueues[found].size > 0
    // Should never happen.
    if(recvqueues[found].size == 0)
    {
        release(&netlock);
        printf("ERROR sys_recv: recvqueues[found].size == 0 after sleep");
        printf(" port: %d\n", dport);
        return -1;
    }

    // dequeue a packet
    char *buf = (char *)recvqueues[found].q[recvqueues[found].head];
    recvqueues[found].head = (recvqueues[found].head + 1) % RECV_QUEUE_SIZE;
    recvqueues[found].size--;
    release(&netlock);

    struct eth *eth = (struct eth *) buf;
    struct ip *ip = (struct ip *)(eth + 1);
    struct udp *udp = (struct udp *)(ip + 1);
    // NOTE: udp->ulen includes header and Data field (payload)
    int updpayloadlen = ntohs(udp->ulen) - sizeof(struct udp);
    // printf("DEBUG: sys_recv: udp payload len = %d\n", updpayloadlen);
    if(updpayloadlen > maxlen)
    {
        kfree(buf);
        printf("ERROR sys_recv: payload len %d > maxlen %d\n", updpayloadlen, maxlen);
        return -1;
    }
    // Copy the payload to user space buf.
    struct proc *p = myproc();
    if(copyout(p->pagetable, bufaddr, (char *)(udp + 1), updpayloadlen) < 0)
    {
        kfree(buf);
        printf("ERROR sys_recv: copyout payload failed\n");
        return -1;
    }
    uint32 srcip = ntohl(ip->ip_src);
    uint16 sport = ntohs(udp->sport);
    if(copyout(p->pagetable, srcaddr, (char *)&srcip, sizeof(srcip)) < 0)
    {
        kfree(buf);
        printf("ERROR sys_recv: copyout srcip failed\n");
        return -1;
    }
    if(copyout(p->pagetable, sportaddr, (char *)&sport, sizeof(sport)) < 0)
    {
        kfree(buf);
        printf("ERROR sys_recv: copyout sport failed\n");
        return -1;
    }
    kfree(buf);
    return updpayloadlen;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

// NOTE: buf is needed to be freed.
// If the packet is not valid, just free it and return.
// If the packet is valid, enqueue it to recvqueue[] and wakeup 
// sys_recv() to dequeue it and free it.
void
ip_rx(char *buf, int len)
{
    // don't delete this printf; make grade depends on it.
    static int seen_ip = 0;
    if(seen_ip == 0)
        printf("ip_rx: received an IP packet\n");
    seen_ip = 1;

    struct eth *eth = (struct eth *) buf;
    struct ip *ip = (struct ip *)(eth + 1);
    struct udp *udp = (struct udp *)(ip + 1);
    // guarantee it's UDP packet.
    if(ip->ip_p != IPPROTO_UDP)
    {
        kfree(buf);
        printf("ip_rx: Not a UDP packet.\n");
        return;
    }
    int found = -1;
    uint16 destport = ntohs(udp->dport);
    for(int i = 0; i < recvqueuesend; i++)
    {
        if(destport == recvqueues[i].port)
        {
            found = i;
            break;
        }
    }
    // guarantee the port is bound i.e., it is listening.
    if (found == -1)
    {
        //----------- BUG TWO -----------
        // WARNING: kfree must be called after printf, or the grader will fail!!!
        // Since udp->dport is in buf!!!!
        printf("ip_rx: UDP packet is not for a bound port %d.\n", ntohs(udp->dport));
        kfree(buf);

        return; 
    }
    printf("DEBUG: ip_rx: received UDP packet for bound port %d\n", destport);
    // Now we this packet is valid packet which is waiting for sys_recv()
    // Check if the recv queue is full.
    acquire(&netlock);
    if(recvqueues[found].size >= RECV_QUEUE_SIZE)
    {
        release(&netlock);
        printf("ip_rx: recv queue is full.\n");
        kfree(buf);
        return;
    }
    // Enqueue it.
    recvqueues[found].q[recvqueues[found].tail] = (uint64)buf;
    recvqueues[found].tail = (recvqueues[found].tail + 1) % RECV_QUEUE_SIZE;
    recvqueues[found].size++;
    release(&netlock);
    // Wake up sys_recv() to dequeue it.
    wakeup(&recvqueues[found]);
    return;
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
