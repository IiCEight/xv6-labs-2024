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
void sfreerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// ksmem is used for superpage
struct {
  struct spinlock lock;
  struct run *freelist; // Insert from head.
} kmem, ksmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange((void*)KSMEMEND, (void*)PHYSTOP);

    // init superpage allocator.
    initlock(&ksmem.lock, "ksmem");
    sfreerange(end, (void*)KSMEMEND);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

void sfreerange(void *pa_start, void *pa_end)
{
    char *p;
    p = (char*)SUPERPGROUNDUP((uint64)pa_start);
    if (p > (char*)pa_end)
        panic("KSMEMEND is too small and end > KSMEMEND");
    for(; p + SUPERPGSIZE <= (char*)pa_end; p += SUPERPGSIZE)
        ksfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (uint64)pa < KSMEMEND || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void ksfree(void *pa)
{
    struct run *r;
    if(((uint64)pa % SUPERPGSIZE) != 0 || (char*)pa < end || (uint64)pa >= KSMEMEND)
        panic("ksfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, SUPERPGSIZE);

    r = (struct run*)pa;

    acquire(&ksmem.lock);
    r->next = ksmem.freelist;
    ksmem.freelist = r;
    release(&ksmem.lock);
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
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void * ksalloc(void)
{
    struct run *r;

    acquire(&ksmem.lock);
    r = ksmem.freelist;
    if(r)
        ksmem.freelist = r->next;
    release(&ksmem.lock);

    if(r)
        memset((char*)r, 5, SUPERPGSIZE); // fill with junk
    return (void*)r;
}
