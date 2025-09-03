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
        kmem.refcount = 0;
        kmem.refcount = (uint16 *)kalloc();
        // initialize it to 0.
        memset(kmem.refcount, 0, PGSIZE);
    }
    for (int i = 0; i < 16 * PGSIZE / 2; i++)
    {
        if(kmem.refcount[i] != 0)
            panic("refcount not zero");
        if((uint64)(kmem.refcount + i) >= PHYSTOP)
        {
            printf("kmem.refcount + i: %lx\n", (uint64)(kmem.refcount + i));
            panic("refcount out of bounds");
        }
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
  // Shit this bug take me about 2hours.
  // I forget to handle this and case
  // clear all pages whoever they are.
 /*  memset(pa, 0, PGSIZE); */

  r = (struct run*)pa;
  int index = MEMINDEX(r);

  acquire(&kmem.lock);

  if(kmem.refcount != 0)
  {
    kmem.refcount[index]--;
    if(kmem.refcount[index] == 0)
    {
        r->next = kmem.freelist;
        kmem.freelist = r;
        memset(pa, 0, PGSIZE);
    }
    // printf("kfree address %lx, index %d, refcount %d\n", (uint64)r, index, kmem.refcount[index]);
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
  int index = MEMINDEX(r);
  if(r)
  {
    kmem.freelist = r->next;
    // update reference count
    if(kmem.refcount != 0)
    {
        if(kmem.refcount[index] != 0)
            panic("kalloc: double kalloc");
        kmem.refcount[index]++;
    }
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

//   printf("kalloc address %lx, index %d, refcount %d\n", (uint64)r, index, kmem.refcount[index]);

  return (void*)r;
}

void kincresememrefcount(uint64 pa)
{
    acquire(&kmem.lock);
    if(kmem.refcount == 0)
        panic("kincresememrefcount: refcount not initialized");
    kmem.refcount[MEMINDEX(pa)] ++;
//   printf("update address %lx, index %ld, refcount %d\n", (uint64)pa, MEMINDEX(pa), kmem.refcount[MEMINDEX(pa)]);
    release(&kmem.lock);
}
