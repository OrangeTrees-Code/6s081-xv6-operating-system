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
#define HASH(blockno) (blockno%NBUCKET)

struct bucket {
  struct spinlock lock;

  struct buf head;
};
struct {
  struct spinlock evicLock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;

  struct bucket buckets[NBUCKET];
} bcache;

void
binit(void) {
  struct buf* b;

  initlock(&bcache.evicLock, "bcache");

  char name[16];
  for (int i = 0;i < NBUCKET;i++) {
    snprintf(name, sizeof(name), "bcache_%d", i);
    initlock(&bcache.buckets[i].lock, name);
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  char nameBuf[16];
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    int blockno = b - bcache.buf;

    snprintf(nameBuf, sizeof(nameBuf), "buffer_%d", blockno);
    initsleeplock(&b->lock, "buffer");
    // bcache.head.next->prev = b;
    // bcache.head.next = b;

    struct buf* h = &(bcache.buckets[HASH(blockno)].head);
    b->next = h->next;
    b->prev = h;

    h->next->prev = b;
    h->next = b;

    b->timeStamp = 0;
    b->blockno = blockno;
  }
}

void TakeBuf(struct bucket* stealer, struct bucket* stealee, struct buf* buf) {

  if (stealer == stealee) {
    acquire(&stealer->lock);
  }
  else if (stealer < stealee) {
    acquire(&stealer->lock);
    acquire(&stealee->lock);
  }
  else {
    acquire(&stealee->lock);
    acquire(&stealer->lock);
  }

  buf->next->prev = buf->prev;
  buf->prev->next = buf->next;

  buf->next = stealer->head.next;
  buf->prev = &stealer->head;
  stealer->head.next->prev = buf;
  stealer->head.next = buf;

  if (stealer == stealee) {
    release(&stealer->lock);
  }
  else if (stealer < stealee) {
    release(&stealee->lock);
    release(&stealer->lock);
  }
  else {
    release(&stealer->lock);
    release(&stealee->lock);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno) {
  struct buf* b;

  // acquire(&bcache.evicLock);
  int index = HASH(blockno);

  struct bucket* bkt = &bcache.buckets[index];

  acquire(&bkt->lock);
  // Is the block already cached?
  for (b = bkt->head.next; b != &bkt->head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      b->timeStamp = ticks;
      release(&bkt->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bkt->lock);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // for (b = bkt->head.prev; b != &bkt->head; b = b->prev) {
  //   if (b->refcnt == 0) {
      
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.evicLock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  acquire(&bcache.evicLock);
  int maxTimestamp = __INT32_MAX__;
  struct buf* replace = 0;
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    if (b->refcnt == 0 && b->timeStamp < maxTimestamp) {
      maxTimestamp = b->timeStamp;
      replace = b;
    }
  }
  if (replace && replace->refcnt == 0) {
    TakeBuf(bkt, &(bcache.buckets[HASH(replace->blockno)]), replace);
    replace->dev = dev;
    replace->blockno = blockno;
    replace->valid = 0;
    replace->refcnt = 1;
    replace->timeStamp = ticks;
    release(&bcache.evicLock);
    acquiresleep(&replace->lock);
    return replace;
  }
  release(&bcache.evicLock);


  
}



// Return a locked buf with the contents of the indicated block.
struct buf*
  bread(uint dev, uint blockno) {
  struct buf* b;

  b = bget(dev, blockno);
  if (!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf* b) {
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf* b) {
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  struct bucket* bkt = &bcache.buckets[HASH(b->blockno)];
  acquire(&bkt->lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->timeStamp = ticks;
  }

  release(&bkt->lock);
}

void
bpin(struct buf* b) {
  acquire(&bcache.evicLock);
  b->refcnt++;
  release(&bcache.evicLock);
}

void
bunpin(struct buf* b) {
  acquire(&bcache.evicLock);
  b->refcnt--;
  release(&bcache.evicLock);
}


