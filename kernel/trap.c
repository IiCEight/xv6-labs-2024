#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

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
  } 
    else if (r_scause() == 15) // page fault.
    {
        printf("page fault begin....\n");
        uint64 va = r_stval(); // the faulting address.
        printf("page fault va = 0x%lx\n", va);
        if(va >= MAXVA)
        {
            printf("page fault: invalid address va >= MAXVA\n");
            setkilled(p);
        }
        else 
        {
            pte_t *pte = walk(p->pagetable, va, 0);
            if(pte == 0) 
            {
                printf("page fault: pte is null - page not mapped at all\n");
                printf("va=0x%lx is outside allocated memory (sz=0x%lx)\n", va, p->sz);
                setkilled(p);
            }
            else if(((*pte) & PTE_V) == 0)
            {
                printf("page fault: PTE exists but PTE_V=0 - page not valid\n");
                printf("pte = 0x%lx\n", *pte);
                setkilled(p);
            }
            else if(((*pte) & PTE_SW) == 0) 
            {
                printf("page fault: not a COW page (PTE_SW=0)\n");
                printf("pte = 0x%lx, flags: V=%d R=%d W=%d X=%d U=%d\n", 
                       *pte,
                       (*pte & PTE_V) ? 1 : 0,
                       (*pte & PTE_R) ? 1 : 0,
                       (*pte & PTE_W) ? 1 : 0,
                       (*pte & PTE_X) ? 1 : 0,
                       (*pte & PTE_U) ? 1 : 0);
                setkilled(p);
            }
            else 
            {
                uint64 rounddownpa = PTE2PA(*pte);
                char *mem = kalloc();
                if(mem == 0)
                {
                    printf("usertrap: out of memory\n");
                    setkilled(p);
                } 
                else 
                {
                    // NOTE: we need page align.
                    uint64 a = PGROUNDDOWN(va);
                    
                    // Copy from the original page using walkaddr to get proper kernel virtual address
                    uint64 ka = walkaddr(p->pagetable, a);
                    if(ka != rounddownpa)
                        panic("Wrong~~~~~~~~~~~~~~~~~~~~~~~~\n");
                    if(ka == 0) {
                        printf("usertrap: walkaddr failed\n");
                        kfree(mem);
                        setkilled(p);
                    } else {
                        memmove(mem, (char*)ka, PGSIZE);
                        if(mappagescopy(p->pagetable, a, PGSIZE, (uint64)mem) != 0)
                        {
                            printf("usertrap: mappages failed\n");
                            kfree(mem);
                            setkilled(p);
                        }
                        else 
                        {
                            // succeed!!
                            // refresh TLB we don't need since we are in
                            // kernel and only can refresh kernel TLB
                            // When return to user mode, os will change
                            // TLB and refresh it.
                            // sfence_vma();

                            // decrease original physical memory reference count
                            kfree((void *)rounddownpa);
                        }
                    }
                }
            }
        }
        printf("page fault end....\n");
    }
    else if (r_scause() == 13) // load page fault
    {
        printf("load page fault!!!");
    }
  else {
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    printf("name: %s\n", p->name);
    setkilled(p);
  }

  if(killed(p))
  {
    printf("I am killed!!!!!!!!!!!!!!!\n");
    exit(-1);
  }

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
    // printf("Return to user mode\n");
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

