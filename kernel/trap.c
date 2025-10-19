#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "fcntl.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "proc.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

// Check if the given virtual address 'va' falls within any of the
// mmap VMAs of the process 'p'.
// Return index of the mmap VMA if found, -1 otherwise.

int checkMmapVMA(struct proc *p, uint64 va) {
    for (int i = 0; i < NMMAPVMA; i++) {
        if (p->mmapvmas[i].length > 0) {
            uint64 start = p->mmapvmas[i].addr;
            uint64 end = start + p->mmapvmas[i].length;
            if (va >= start && va < end) {
                return i;
            }
        }
    }
    return -1;
}

// print the contents of a physical page
void printPhysicalPage(uint64 pa) {
    char *p = (char *)pa;
    char last = '\n', now;
    int count = 1;
    for (char i = 0; i < PGSIZE; i++) {
        now = *(p + i);
        if(last == now) {
            count++;
        } else {
            printf("%d x %d", (int)last, count);
            printf("\n");
            last = now;
            count = 1;
        }
    }
    printf("\n");
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else if (r_scause() == 15 || r_scause() == 13) {
    // page fault
    uint64 va = r_stval(); // the faulting address.
    printf("Page fault at address 0x%lx, pid=%d\n", va, p->pid);
    uint64 pa = walkaddr(p->pagetable, va);
    if(pa != 0) {
        panic("But the address is already mapped to physical address\n");
    }
    int vmaIndex = checkMmapVMA(p, va);
    if (vmaIndex == -1) {
        printf("No mmap VMA found for address 0x%lx, pid=%d\n", va, p->pid);
        setkilled(p);
    } else {
        // Handle the page fault by reading 4096 bytes
        // of the relevant file into that page.
        struct mmapvma *vma = &p->mmapvmas[vmaIndex];
        printf("File size %d bytes\n", vma->file->ip->size);
        uint64 vaRoundDown = PGROUNDDOWN(va);
        // since we only support offset zero
        uint64 offsetInFile = vaRoundDown - vma->addr + vma->offset;
        printf("Offset in file: 0x%lx\n", offsetInFile);

        int perm = PTE_U | PTE_V;
        if (vma->prot & PROT_READ) perm |= PTE_R;
        if (vma->prot & PROT_WRITE) perm |= PTE_W;

        // Allocate a new page
        uint64 pa = (uint64)kalloc();
        if (pa == 0) {
            printf("kalloc failed for mmap at address 0x%lx, pid=%d\n", vaRoundDown, p->pid);
            setkilled(p);
        } else {
            // NOTE: file size is not page-aligned
            // We should zero the page first
            memset((void*)pa, 0, PGSIZE);
            // Load data from file into the allocated page
            if (vma->file->readable == 0) {
                panic("mmap VMA file is not readable");
            }
            struct inode *ip = vma->file->ip;
            ilock(ip);
            if (readi(ip, 0, pa, offsetInFile, PGSIZE) < 0) {
                printf("readi failed for mmap at address 0x%lx, pid=%d\n", vaRoundDown, p->pid);
                iunlock(ip);
                kfree((void*)pa);
                setkilled(p);
            } else {
                iunlock(ip);
                // Map the page into the process's page table
                if (mappages(p->pagetable, vaRoundDown, PGSIZE, pa, perm) < 0) {
                    printf("mappages failed for mmap at address 0x%lx, pid=%d\n", vaRoundDown, p->pid);
                    kfree((void*)pa);
                    setkilled(p);
                }
            }
        }
        
    }

    printf("------ Page fault end ------\n");
  } else {
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if(scause == 0x8000000000000009L){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // timer interrupt.
    clockintr();
    return 2;
  } else {
    return 0;
  }
}

