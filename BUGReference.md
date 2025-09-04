# BUG Reference

**This lab is hard since it is easy to get wrong.**

Here I meet **SIX** bugs when I complete my code first time.

```c
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

    /********** bug 1: here *************/
  // Fill with junk to catch dangling refs.
  // Shit this bug take me about 2 hours.
  // I forget to handle this and cause
  // clear all pages whoever they are.
 /*  memset(pa, 0, PGSIZE); */

  r = (struct run*)pa;
  int index = MEMINDEX(r);

  acquire(&kmem.lock);
  acquire(&refcountlock);

  if(kmem.refcount != 0)
  {
    /********** bug 3: here *************/
    // NOTE: If refcount is unsigned and = 0.
    // Then kmem.refcount[index]--. It will
    // underflow to 65535.
    kmem.refcount[index]--;
    if(kmem.refcount[index] == 0)
    {
         /********** bug 2: here *************/
        // NOTE!!!!!!!!
        // Order is important here.
        // clear must before r->next = kmem.freelist;
        // Or r->next will be 0;
        memset(pa, 0, PGSIZE);
        r->next = kmem.freelist;
        kmem.freelist = r;
    }
    // printf("kfree address %lx, index %d, refcount %d\n", (uint64)r, index, kmem.refcount[index]);
  }
  else
  {
    memset(pa, 0, PGSIZE);
    r->next = kmem.freelist;
    kmem.freelist = r;
  }
  release(&refcountlock);
  release(&kmem.lock);
}
```

### 1.Clear all pages whoever they are

Whne I modify `kfree`. I forget to move `memset(pa, 0, PGSIZE);` to proper place.



### 2.Place memset(pa, 0, PGSIZE); to wrong place

```c
//  This it wrong
    r->next = kmem.freelist;
    kmem.freelist = r;
    memset(pa, 0, PGSIZE);
```

`memset` must before `r->next = kmem.freelist`, or `r->next` will be `0`.

**This is a really subtle BUG.**



### 3.Unsigned short underflow

If  ` kmem.refcount[index];` is not initialized correctly and do `    kmem.refcount[index]--;`.

It will underflow to 65535 if `kmem.refcount[index] == 0`.

So I must maintain `kmem.refcount` carefully for every time.



### 4.Set every page as PTE_SW

```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
//   char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    /*************** bug 4: Here ******************/
    // Clear PTE_W for both parent and child
    // NOTE: We need to identify the page as writable
    // by using a bit (PTE_SW).
    if ((*pte) & PTE_W)
    {
        *pte &= (~PTE_W);
        *pte |= PTE_SW;
        if((*pte) & PTE_W)
            panic("uvmcopy: PTE_W not cleared!");
    }

    flags = PTE_FLAGS(*pte);
    // We share the physical memory.
    // if((mem = kalloc()) == 0)
    //   goto err;
    // memmove(mem, (char*)pa, PGSIZE);

    
    if(mappages(new, i, PGSIZE, pa, flags) != 0){
        //   kfree(mem);
        goto err;
    }
    kincresememrefcount(pa);
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

When orignal page is wriable, I can clear `PTE_W` and set `PTE_SW`.

**The `if ((*pte) & PTE_W)` check is necessary.**

I noticed this condition when I first code. However I delete it when I see other's code which marks all page as `PTE_SW`. **What a pity!!!**



### 5.Ignore load page fault

```c
    /*************** bug 5: Here ******************/
  // BUG FOUND: Load page fault is need handle too.
    else if (r_scause() == 15 || r_scause() == 13) // page fault and load page fault
    {
        // printf("page fault begin....\n");
        uint64 va = r_stval(); // the faulting address.
        // printf("page fault va = 0x%lx\n", va);
        if(pagefaultcheck(p->pagetable, va) == 0)
        {
            printf("pagefaultcheck: failed\n");
            setkilled(p);
        }
        else
        {
		...
		}
	}...
```

I don't consider `r_scause() == 13`. And because of that, `sbrkfail()` test will fail.

