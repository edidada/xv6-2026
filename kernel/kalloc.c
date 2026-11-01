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
  int count;
} kmem[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; i++) initlock(&kmem[i].lock, "kmem");
  freerange(end, (void*)PHYSTOP);
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
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  __atomic_fetch_add(&kmem[id].count, 1, __ATOMIC_RELAXED);
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r){
    kmem[id].freelist = r->next;
    __atomic_fetch_sub(&kmem[id].count, 1, __ATOMIC_RELAXED);
  }
  release(&kmem[id].lock);
  while(!r){
    // Prefer a well-stocked list over another CPU's last reusable page.
    // Counts are hints only; the donor lock still protects the actual list.
    int donor = -1, most = 0;
    for(int n = 1; n < NCPU; n++){
      int candidate = (id+n)%NCPU;
      int count = __atomic_load_n(&kmem[candidate].count, __ATOMIC_RELAXED);
      if(count > most){ most = count; donor = candidate; }
    }
    if(donor < 0) break;
    acquire(&kmem[donor].lock);
    struct run *batch = kmem[donor].freelist;
    if(batch){
      struct run *last = batch;
      int take = (__atomic_load_n(&kmem[donor].count, __ATOMIC_RELAXED)+1)/2;
      if(take > 256) take = 256;
      for(int j = 1; j < take; j++) last = last->next;
      kmem[donor].freelist = last->next;
      __atomic_fetch_sub(&kmem[donor].count, take, __ATOMIC_RELAXED);
      last->next = 0;
      release(&kmem[donor].lock);
      r = batch;
      if(batch->next){
        acquire(&kmem[id].lock);
        last->next = kmem[id].freelist;
        kmem[id].freelist = batch->next;
        __atomic_fetch_add(&kmem[id].count, take-1, __ATOMIC_RELAXED);
        release(&kmem[id].lock);
      }
    } else {
      release(&kmem[donor].lock);
    }
  }
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
