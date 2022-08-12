// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

struct bucket {
  struct spinlock lock;
  struct buf head;
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct bucket bk[NBUCKET];
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.bk[i].lock, "bcache_bk");
    bcache.bk[i].head.prev = &bcache.bk[i].head;
    bcache.bk[i].head.next = &bcache.bk[i].head;
  }

  for (int i = 0; i < NBUF; i++) {
    int index = i % NBUCKET;
    b = &bcache.buf[i];
    b->next = bcache.bk[index].head.next;
    b->prev = &bcache.bk[index].head;

    bcache.bk[index].head.next->prev = b;
    bcache.bk[index].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int index = blockno % NBUCKET;
  acquire(&bcache.bk[index].lock);
  // Is the block already cached?
  // 在当前的 bucket 里查看有没有缓存当前块
  for (b = bcache.bk[index].head.next; b != &bcache.bk[index].head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.bk[index].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // 在当前的 bucket 里查看有没有空闲块，有的话直接用空闲的块
  for (b = bcache.bk[index].head.prev; b != &bcache.bk[index].head; b = b->prev) {
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.bk[index].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bk[index].lock);
  // 当前的 bucket 没有空闲块了，去其他的 bucket 里拿些空闲的块
  // 获取 global lock
  acquire(&bcache.lock);
  // 找到的空闲块
  struct buf *free_buf = 0;
  // 空闲块所在的 bucket
  int free_bk = -1;

  for (int i = 0; i < NBUCKET; i++) {
    acquire(&bcache.bk[i].lock);

    if (i == index) {
      release(&bcache.bk[i].lock);
      continue;
    }

    // 从 bucket 里寻找空闲块
    for (b = bcache.bk[i].head.prev; b != &bcache.bk[i].head; b = b->prev) {
      if(b->refcnt == 0) {
        free_buf = b;
        free_bk = i;
      }
    }

    // 找到空闲的块了
    if (free_bk != -1) {
      // 仍持有锁
      break;
    }

    release(&bcache.bk[i].lock);
  }

  acquire(&bcache.bk[index].lock);

  if (free_bk != -1) {
    // hold lock: bcache.lock, bcache.bk[free_bk].lock
    // 先查看当前的的 bucket
    for (b = bcache.bk[index].head.next; b != &bcache.bk[index].head; b = b->next) {
      if (b->dev == dev && b->blockno == blockno) {
        b->refcnt++;
        release(&bcache.bk[index].lock);
        release(&bcache.bk[free_bk].lock);
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
      }
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    // 在当前的 bucket 里查看有没有空闲块，有的话直接用空闲的块
    for (b = bcache.bk[index].head.prev; b != &bcache.bk[index].head; b = b->prev) {
      if(b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&bcache.bk[index].lock);
        release(&bcache.bk[free_bk].lock);
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
      }
    }

    // 当前的 bucket 还是没有
    // 在 free_bk 里删掉 free_buf;
    free_buf->next->prev = free_buf->prev;
    free_buf->prev->next = free_buf->next;

    // 移到当前 bucket 里
    free_buf->next = bcache.bk[index].head.next;
    free_buf->prev = &bcache.bk[index].head;
    bcache.bk[index].head.next->prev = free_buf;
    bcache.bk[index].head.next = free_buf;

    free_buf->dev = dev;
    free_buf->blockno = blockno;
    free_buf->valid = 0;
    free_buf->refcnt = 1;

    release(&bcache.bk[index].lock);
    release(&bcache.bk[free_bk].lock);
    release(&bcache.lock);
    acquiresleep(&free_buf->lock);

    return free_buf;
  } else {
    // hold lock: bcache.lock
    // 再次查看当前的 bucket
    // Is the block already cached?
    // 在当前的 bucket 里查看有没有缓存当前块
    for (b = bcache.bk[index].head.next; b != &bcache.bk[index].head; b = b->next) {
      if (b->dev == dev && b->blockno == blockno) {
        b->refcnt++;
        release(&bcache.bk[index].lock);
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
      }
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    // 在当前的 bucket 里查看有没有空闲块，有的话直接用空闲的块
    for (b = bcache.bk[index].head.prev; b != &bcache.bk[index].head; b = b->prev) {
      if(b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&bcache.bk[index].lock);
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
      }
    }

    // 还是没有
    printf("can not find a free block\n");
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int index = b->blockno % NBUCKET;
  acquire(&bcache.bk[index].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.bk[index].head.next;
    b->prev = &bcache.bk[index].head;
    bcache.bk[index].head.next->prev = b;
    bcache.bk[index].head.next = b;
  }
  
  release(&bcache.bk[index].lock);
}

void
bpin(struct buf *b) {
  int index = b->blockno % NBUCKET;
  acquire(&bcache.bk[index].lock);
  b->refcnt++;
  release(&bcache.bk[index].lock);
}

void
bunpin(struct buf *b) {
  int index = b->blockno % NBUCKET;
  acquire(&bcache.bk[index].lock);
  b->refcnt--;
  release(&bcache.bk[index].lock);
}


