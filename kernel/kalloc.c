// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "spinlock.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "kalloc.h"


void freerange(void *pa_dtb);
extern void* kalloc_numa(void);
extern void kfree_numa(void*);
char numa_ready = 0;

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct kmem kmem;


void
kinit(void* pa_dtb)
{
  initlock(&kmem.lock, "kmem");
  freerange(pa_dtb);
}


// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  // if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
  if(((uint64)pa % PGSIZE) != 0)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  if(!numa_ready){
    r = (struct run*)pa;
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  else{
    kfree_numa(pa);
  }
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  if(!numa_ready){
    acquire(&kmem.lock);
    r = kmem.freelist;
    if(r)
      kmem.freelist = r->next;
    release(&kmem.lock);
  }
  else{
    r = kalloc_numa();
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  
  return (void*)r;
}
