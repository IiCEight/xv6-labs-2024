// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// Forward declare so the prototype sees the same tag as the later definition.
struct kernelmem;
void freerange(struct kernelmem *kmem_ptr, void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kernelmem{
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

int getCpuId() {
    // Turn off interrupts
    push_off();
    int id = cpuid();
    pop_off();
    return id;
}

void getKmemLockName(char *lockname, int len) {
  // Turn off interrupts
  push_off();
  int id = cpuid();
  pop_off();
  snprintf(lockname, len, "kmem%d", id);
}

void
kinit()
{
    // Turn off interrupts
    char lockname[16];
    getKmemLockName(lockname, sizeof(lockname));
    printf("lockname: %s\n", lockname);
    int id = getCpuId();
    initlock(&kmem[id].lock, lockname);

    freerange(&kmem[id], end, (void*)PHYSTOP);
}

void
freerange(struct kernelmem *kmem_ptr, void *pa_start, void *pa_end)
{
    char *p;
    p = (char*)PGROUNDUP((uint64)pa_start);
    char *pstart = p;
    char *pend = (char*)PGROUNDDOWN((uint64)pa_end);
    int eachPageNum = (int)(((uint64)(pend - pstart)) / PGSIZE / NCPU);
    printf("eachPageNum: %d\n", eachPageNum);
    int id = getCpuId();

        // For debug
    if (id == 0) {
        printf("All pages: %d\n", (int)((pend-pstart) / PGSIZE));
        printf("Number of CPUs: %d \n", NCPU);
    }

  // allocate each cpu its own pages
    p += eachPageNum * PGSIZE * id;
    char * endaddr = (char*)(PGSIZE * eachPageNum * (id + 1) + pstart);
    if (id == NCPU - 1) {
        endaddr = pend;
    }
    for(; p + PGSIZE <= endaddr; p += PGSIZE)
        kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
    int id = getCpuId();

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
    struct run *r;
    int id = getCpuId();

    acquire(&kmem[id].lock);
    r = kmem[id].freelist;
    if(r)
        kmem[id].freelist = r->next;
    release(&kmem[id].lock);

    // No free page, try to steal from other cpus
    // NOTE: We need to release the lock of current cpu 
    // before acquiring other cpu's lock. Otherwise it may
    // lead to deadlock.
    if (r == 0)
    {
        for(int i = 0; i < NCPU; i++) 
        {
            if(i == id)
            continue;
            acquire(&kmem[i].lock);
            r = kmem[i].freelist;
            if(r) {
                kmem[i].freelist = r->next;
                release(&kmem[i].lock);
                break;
            }
            release(&kmem[i].lock);
        }
    }

    if(r)
        memset((char*)r, 5, PGSIZE); // fill with junk
    return (void*)r;
}
