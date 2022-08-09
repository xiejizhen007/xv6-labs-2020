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
  int freepg_count;     // 记录当前 cpu 空闲页的数量
} kmem[NCPU];

void
kinit()
{
  for (int id = 0; id < NCPU; id++) {
    initlock(&kmem[id].lock, "kmem");
    kmem[id].freepg_count = 0;
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
  // push_off();
  // printf("cpu[%d]'s free page size: %d\n", cpuid(), kmem[cpuid()].freepg_count);
  // pop_off();
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // turn interrupts off, get a current core number
  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freepg_count++;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  // turn interrupts on
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
  // 当前 cpu's freelist 仍有空闲页
  if (kmem[id].freepg_count > 0) {
    r = kmem[id].freelist;
    kmem[id].freelist = r->next;
    kmem[id].freepg_count--;
  } else {
    // 没空闲页了，去其他 cpu's freelist 偷
    // printf("cpu[%d] no free page\n", id);
    // 存在空闲页的 cpuid
    int find_cpu = -1;
    // 遍历所有 cpu，从中寻找存在空闲页的 cpuid
    for (int i = 0; i < NCPU; i++) {
      if (i == id) {
        // 当前肯定是没有的了
        continue;
      }

      acquire(&kmem[i].lock);
      // 存在空闲页？
      if (kmem[i].freepg_count > 0) {
        find_cpu = i;
      }
      release(&kmem[i].lock);

      if (find_cpu != -1) {
        // 找到了
        break;
      }
    }

    // 其他 cpu 中存在空闲页，去他那偷
    if (find_cpu != -1) {
      // new id
      int nid = find_cpu;
      // printf("find a free page in cpu[%d] freelist\n", nid);
      acquire(&kmem[nid].lock);
      r = kmem[nid].freelist;
      kmem[nid].freepg_count--;
      kmem[nid].freelist = r->next;
      release(&kmem[nid].lock);
    } else {
      // 实在是没有空闲的页了
      r = 0;
    }
  }
  
  release(&kmem[id].lock);
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
