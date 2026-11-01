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

struct pagepool {
  struct spinlock lock;
  struct run *freelist;
  int count;
};
static struct {
  struct pagepool hot;
  struct pagepool cold;
} kmem[NCPU];

// Caller holds pool->lock. Detach at most limit pages as one batch.
static struct run *
detach(struct pagepool *pool, int limit, int *count, struct run **tail)
{
  struct run *first = pool->freelist;
  *count = 0;
  if(!first) return 0;
  *tail = first;
  *count = 1;
  while(*count < limit && (*tail)->next){
    *tail = (*tail)->next;
    (*count)++;
  }
  pool->freelist = (*tail)->next;
  (*tail)->next = 0;
  pool->count -= *count;
  return first;
}

static struct run *
take(struct pagepool *pool, int limit, int *count, struct run **tail)
{
  acquire(&pool->lock);
  struct run *first = detach(pool, limit, count, tail);
  release(&pool->lock);
  return first;
}

static void
put(struct pagepool *pool, struct run *first, struct run *tail, int count)
{
  acquire(&pool->lock);
  tail->next = pool->freelist;
  pool->freelist = first;
  pool->count += count;
  release(&pool->lock);
}

void
kinit()
{
  for(int i = 0; i < NCPU; i++){
    initlock(&kmem[i].hot.lock, "kmem-hot");
    initlock(&kmem[i].cold.lock, "kmem-cold");
  }
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
  struct pagepool *hot = &kmem[id].hot;
  struct run *batch = 0, *tail = 0;
  int count = 0;
  acquire(&hot->lock);
  r->next = hot->freelist;
  hot->freelist = r;
  hot->count++;
  // Keep a small hot cache; bulk frees go to the separately locked pool.
  if(hot->count >= 16) batch = detach(hot, 8, &count, &tail);
  release(&hot->lock);
  if(batch) put(&kmem[id].cold, batch, tail, count);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r, *tail = 0;
  int count = 0;

  push_off();
  int id = cpuid();
  r = take(&kmem[id].hot, 1, &count, &tail);
  for(int n = 0; !r && n < NCPU; n++)
    r = take(&kmem[(id+n)%NCPU].cold, 8, &count, &tail);
  // Reclaim even the last cached pages when all bulk pools are empty.
  // This slow path preserves the ability to allocate all physical memory.
  for(int n = 1; !r && n < NCPU; n++)
    r = take(&kmem[(id+n)%NCPU].hot, 1, &count, &tail);
  if(r && count > 1){
    struct run *rest = r->next;
    r->next = 0;
    put(&kmem[id].hot, rest, tail, count-1);
  }
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
