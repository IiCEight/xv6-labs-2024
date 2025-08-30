#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

// using global ticks
// this is in defs.h
// extern uint ticks;

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  backtrace();
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


uint64
sys_sigalarm(void)
{
    struct proc *p = myproc();
    int interval;
    uint64 handler;
    
    argint(0, &interval);
    argaddr(1, &handler);
    
    p->sigalarminterval = interval;
    // Store user virtual address directly
    // Since user code cann't be run in kernel.
    // It should run in user space.
    p->sigalarmhandler = (void (*)(void))handler;
    p->passedticks = 0;

    return 0;
}

uint64
sys_sigreturn(void)
{
    struct proc *p = myproc();
    if(p->needReturn == 0)
    return 0;
    
    // printf("begin sigreturn........\n");
    
    p->needReturn = 0;
    // according sigtrapframe to resume context
    p->trapframe->epc = p->sigtrapframe->epc;
    p->trapframe->ra = p->sigtrapframe->ra;
    p->trapframe->sp = p->sigtrapframe->sp;
    p->trapframe->gp = p->sigtrapframe->gp;
    p->trapframe->tp = p->sigtrapframe->tp;
    p->trapframe->t0 = p->sigtrapframe->t0;
    p->trapframe->t1 = p->sigtrapframe->t1;
    p->trapframe->t2 = p->sigtrapframe->t2;
    p->trapframe->s0 = p->sigtrapframe->s0;
    p->trapframe->s1 = p->sigtrapframe->s1;
    p->trapframe->a0 = p->sigtrapframe->a0;
    p->trapframe->a1 = p->sigtrapframe->a1;
    p->trapframe->a2 = p->sigtrapframe->a2;
    p->trapframe->a3 = p->sigtrapframe->a3;
    p->trapframe->a4 = p->sigtrapframe->a4;
    p->trapframe->a5 = p->sigtrapframe->a5;
    p->trapframe->a6 = p->sigtrapframe->a6;
    p->trapframe->a7 = p->sigtrapframe->a7;
    p->trapframe->s2 = p->sigtrapframe->s2;
    p->trapframe->s3 = p->sigtrapframe->s3;
    p->trapframe->s4 = p->sigtrapframe->s4;
    p->trapframe->s5 = p->sigtrapframe->s5;
    p->trapframe->s6 = p->sigtrapframe->s6;
    p->trapframe->s7 = p->sigtrapframe->s7;
    p->trapframe->s8 = p->sigtrapframe->s8;
    p->trapframe->s9 = p->sigtrapframe->s9;
    p->trapframe->s10 = p->sigtrapframe->s10;
    p->trapframe->s11 = p->sigtrapframe->s11;
    p->trapframe->t3 = p->sigtrapframe->t3;
    p->trapframe->t4 = p->sigtrapframe->t4;
    p->trapframe->t5 = p->sigtrapframe->t5;
    p->trapframe->t6 = p->sigtrapframe->t6;
    return 0;
}