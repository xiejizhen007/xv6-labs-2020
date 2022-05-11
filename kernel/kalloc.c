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

// 引用计数，用于 cow，只有在引用计数为 0 时才释放物理内存
int ref_count[REFCOUNT];

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  // 初始化引用计数
  for (int i = 0; i < REFCOUNT; i++) {
    ref_count[i] = 0;
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

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  r = (struct run*)pa;

  acquire(&kmem.lock);
  int index = REFINDEX((uint64)pa);
  ref_count[index] -= 1;
  if (ref_count[index] <= 0) {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);
    // 只有引用计数为 0 时才真正的释放
    r->next = kmem.freelist;
    kmem.freelist = r;
    ref_count[index] = 0;
  }
  // r->next = kmem.freelist;
  // kmem.freelist = r;
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

  if(r) {
    // 新申请的空间，引用计数设为 1
    ref_count[REFINDEX((uint64)r)] = 1;
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}

int
inc_ref_count(uint64 pa) {
  acquire(&kmem.lock);
  ref_count[REFINDEX(pa)] += 1;
  release(&kmem.lock);
  return ref_count[REFINDEX(pa)];
}

int
kalloc_on_write(pagetable_t pagetable, uint64 va) {
  if (va >= MAXVA) {
    printf("out of memory\n");
    return -1;
  }

  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return -1;
  }
  uint64 pa = PTE2PA(*pte);
  int index = REFINDEX(pa);
  int flags = PTE_FLAGS(*pte);
  acquire(&kmem.lock);      // 处理共享数据，申请锁
  if (ref_count[index] == 1) {
    *pte |= PTE_W;
  } else {
    struct run *r = kmem.freelist;
    if (r) {
      kmem.freelist = r->next;
      ref_count[REFINDEX((uint64)r)] = 1;
      memmove((void *)r, (void *)pa, PGSIZE);
    } else {
      release(&kmem.lock);
      return -1;
    }

    // 将原来的物理页引用计数值减一，要不然无法释放物理页
    ref_count[index] -= 1;
    *pte = PA2PTE((uint64)r) | flags | PTE_W;
  }
  release(&kmem.lock);
  return 0;
}