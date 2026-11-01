#include "types.h"
#include "param.h"
#include "riscv.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "stat.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

uint64
sys_mmap(void)
{
  uint64 addr, len, offset;
  int prot, flags, fd;
  argaddr(0, &addr);
  argaddr(1, &len);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argaddr(5, &offset);
  struct proc *p = myproc();
  if(addr || offset || !len || len > (1UL<<32) ||
     (prot & ~(PROT_READ|PROT_WRITE)) || !prot ||
     (flags != MAP_SHARED && flags != MAP_PRIVATE) ||
     fd < 0 || fd >= NOFILE) return -1;
  struct file *f = p->ofile[fd];
  if(!f || f->type != FD_INODE || !f->readable ||
     ((prot & PROT_WRITE) && flags == MAP_SHARED && !f->writable)) return -1;
  ilock(f->ip);
  int regular = f->ip->type == T_FILE;
  iunlock(f->ip);
  if(!regular) return -1;
  struct vma *v = 0;
  for(int i = 0; i < NVMA; i++)
    if(!p->vmas[i].file){ v = &p->vmas[i]; break; }
  if(!v) return -1;
  len = PGROUNDUP(len);
  addr = MMAPBASE;
  // First-fit search through the fixed-size VMA table.
  for(int retry = 0; retry <= NVMA; retry++){
    int overlap = 0;
    for(int i = 0; i < NVMA; i++){
      struct vma *other = &p->vmas[i];
      if(other->file && addr < other->start+other->length && addr+len > other->start){
        addr = other->start+other->length;
        overlap = 1;
        break;
      }
    }
    if(!overlap) break;
  }
  if(addr+len < addr || addr+len >= TRAPFRAME) return -1;
  *v = (struct vma){addr, len, offset, prot, flags, filedup(f)};
  return addr;
}

uint64
mmapfault(pagetable_t pt, uint64 va, int reading)
{
  struct proc *p = myproc();
  if(pt != p->pagetable || va >= TRAPFRAME) return 0;
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];
    if(!v->file || va < v->start || va-v->start >= v->length) continue;
    if(reading ? !(v->prot & PROT_READ) : !(v->prot & PROT_WRITE)) return 0;
    va = PGROUNDDOWN(va);
    if(ismapped(pt, va)) return 0;
    char *mem = kalloc();
    if(!mem) return 0;
    memset(mem, 0, PGSIZE);
    ilock(v->file->ip);
    uint64 off = v->offset+va-v->start;
    int n = off >= v->file->ip->size ? 0 : readi(v->file->ip, 0, (uint64)mem, off, PGSIZE);
    iunlock(v->file->ip);
    int perm = PTE_U | PTE_R; // RISC-V does not support W without R.
    if(v->prot & PROT_WRITE) perm |= PTE_W;
    if(n < 0 || mappages(pt, va, PGSIZE, (uint64)mem, perm) < 0){
      kfree(mem);
      return 0;
    }
    return (uint64)mem;
  }
  return 0;
}

int
vmaunmap(struct proc *p, uint64 addr, uint64 len)
{
  if(addr % PGSIZE || !len || len > TRAPFRAME || addr+len < addr) return -1;
  len = PGROUNDUP(len);
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];
    if(!v->file || addr < v->start || addr+len > v->start+v->length) continue;
    if(addr != v->start && addr+len != v->start+v->length) return -1;
    for(uint64 a = addr; a < addr+len; a += PGSIZE){
      pte_t *pte = walk(p->pagetable, a, 0);
      if(!pte || !(*pte & PTE_V)) continue;
      if(v->flags == MAP_SHARED && (*pte & PTE_D)){
        uint64 off = v->offset+a-v->start;
        // One disk block per transaction stays below the log limit.
        for(uint n = 0; n < PGSIZE; n += BSIZE){
          begin_op();
          ilock(v->file->ip);
          uint64 size = v->file->ip->size;
          int count = off+n >= size ? 0 : (size-off-n < BSIZE ? size-off-n : BSIZE);
          int wrote = count ? writei(v->file->ip, 0, PTE2PA(*pte)+n, off+n, count) : 0;
          iunlock(v->file->ip);
          end_op();
          if(wrote != count) return -1;
        }
      }
      uvmunmap(p->pagetable, a, 1, 1);
    }
    sfence_vma();
    if(len == v->length){
      struct file *f = v->file;
      memset(v, 0, sizeof(*v));
      fileclose(f);
    } else {
      if(addr == v->start){ v->start += len; v->offset += len; }
      v->length -= len;
    }
    return 0;
  }
  return -1;
}

uint64
sys_munmap(void)
{
  uint64 addr, len;
  argaddr(0, &addr);
  argaddr(1, &len);
  return vmaunmap(myproc(), addr, len);
}

void
vmaclose(struct proc *p)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];
    if(!v->file) continue;
    if(vmaunmap(p, v->start, v->length) < 0){
      // Exit/exec must still reclaim resources after an I/O failure.
      uvmunmap(p->pagetable, v->start, v->length/PGSIZE, 1);
      fileclose(v->file);
      memset(v, 0, sizeof(*v));
    }
  }
}

int
vmacopy(struct proc *parent, struct proc *child)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &parent->vmas[i];
    if(!v->file) continue;
    for(uint64 a = v->start; a < v->start+v->length; a += PGSIZE){
      pte_t *pte = walk(parent->pagetable, a, 0);
      if(!pte || !(*pte & PTE_V)) continue;
      char *mem = kalloc();
      if(!mem) goto bad;
      memmove(mem, (void *)PTE2PA(*pte), PGSIZE);
      if(mappages(child->pagetable, a, PGSIZE, (uint64)mem, PTE_FLAGS(*pte)) < 0){
        kfree(mem);
        goto bad;
      }
    }
  }
  for(int i = 0; i < NVMA; i++){
    child->vmas[i] = parent->vmas[i];
    if(child->vmas[i].file) filedup(child->vmas[i].file);
  }
  return 0;
bad:
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &parent->vmas[i];
    if(v->file) uvmunmap(child->pagetable, v->start, v->length/PGSIZE, 1);
  }
  return -1;
}
