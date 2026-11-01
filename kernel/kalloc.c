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
} kmem;

// A 32 MiB reserve also lends individual pages under memory pressure.
// Per-page allocation bits let demoted superpages be freed a page at a time.
#define NSUPER 16
#define SUPERBASE (PHYSTOP - NSUPER * SUPERPGSIZE)
static struct {
  struct spinlock lock;
  uchar used[NSUPER][512];
} smem;

void *
superalloc(void)
{
  acquire(&smem.lock);
  for(int i = 0; i < NSUPER; i++){
    int j;
    for(j = 0; j < 512 && !smem.used[i][j]; j++) ;
    if(j == 512){
      memset(smem.used[i], 1, 512);
      release(&smem.lock);
      return (void *)(SUPERBASE + (uint64)i * SUPERPGSIZE);
    }
  }
  release(&smem.lock);
  return 0;
}

void
superfree(void *pa)
{
  uint64 addr = (uint64)pa;
  if(addr < SUPERBASE || addr >= PHYSTOP || addr % SUPERPGSIZE)
    panic("superfree");
  acquire(&smem.lock);
  int i = (addr - SUPERBASE) / SUPERPGSIZE;
  for(int j = 0; j < 512; j++)
    if(!smem.used[i][j]) panic("superfree unused");
  memset(pa, 1, SUPERPGSIZE);
  memset(smem.used[i], 0, 512);
  release(&smem.lock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&smem.lock, "supermem");
  freerange(end, (void*)SUPERBASE);
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

  if((uint64)pa >= SUPERBASE){
    uint64 n = ((uint64)pa - SUPERBASE) / PGSIZE;
    acquire(&smem.lock);
    if(!smem.used[n/512][n%512]) panic("kfree super unused");
    memset(pa, 1, PGSIZE);
    smem.used[n/512][n%512] = 0;
    release(&smem.lock);
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
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
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(!r){
    acquire(&smem.lock);
    for(int i = 0; i < NSUPER && !r; i++){
      for(int j = 0; j < 512; j++){
        if(!smem.used[i][j]){
          smem.used[i][j] = 1;
          r = (struct run *)(SUPERBASE + (uint64)i * SUPERPGSIZE + j * PGSIZE);
          break;
        }
      }
    }
    release(&smem.lock);
  }
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
