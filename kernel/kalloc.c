// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  uint16 *refcount;
} kmem;


void
kinit()
{
    printf("freerange begin\n");
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
    printf("init refcount begin\n");
  // Since it is head insertions.
  // And we need continuous physical memory.
    for (int i = 0; i < 16; i++)
    {
        // we need set kmem.refcount to 0 for kalloc();
        acquire(&kmem.lock);
        kmem.refcount = 0;
        release(&kmem.lock);
        kmem.refcount = (uint16 *)kalloc();
        // initialize it to 0.
        memset(kmem.refcount, 0, PGSIZE);
    }
    printf("init kernel memory done.....\n");
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
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

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 0, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);

  if((uint64)kmem.refcount != 0)
  {
    kmem.refcount[MEMINDEX(r)]--;
    if(kmem.refcount[MEMINDEX(r)] == 0)
    {
        r->next = kmem.freelist;
        kmem.freelist = r;
    }
  }
  else
  {
    r->next = kmem.freelist;
    kmem.freelist = r;
  }
  release(&kmem.lock);
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
  {
    kmem.freelist = r->next;
    // update reference count
    if((uint64)kmem.refcount != 0)
        kmem.refcount[MEMINDEX(r)]++;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void kupdatememrefcount(uint64 pa, uint16 variation)
{
    acquire(&kmem.lock);
    if((uint64)kmem.refcount == 0)
        panic("kupdatememrefcount: refcount not initialized");
    kmem.refcount[MEMINDEX(pa)] += variation;
    release(&kmem.lock);
}
