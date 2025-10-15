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


struct buf buf[NBUF];

// Use hash table to spread the buffer cache
// to different buckets which have its own lock
// to reduce lock contention
struct hashbucket {
    struct spinlock lock;
    struct buf head;
};

struct {
    // global lock is only used
    // for changing hash table structure
    struct spinlock lock;

    struct hashbucket buckets[NCACHEHASHBUCKET];

} bcache;


void
binit(void)
{
  initlock(&bcache.lock, "bcacheGlobal");

  // Create linked list of buffers
  for(int i = 0; i < NCACHEHASHBUCKET; i++){
    char bucketLockName[16];
    snprintf(bucketLockName, sizeof(bucketLockName), "bcache_%d", i);
    initlock(&bcache.buckets[i].lock, bucketLockName);
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }
  for(int i=0; i<NBUF; i++){
    int idx = i % NCACHEHASHBUCKET;
    buf[i].next = bcache.buckets[idx].head.next;
    buf[i].prev = &bcache.buckets[idx].head;
    bcache.buckets[idx].head.next->prev = &buf[i];
    bcache.buckets[idx].head.next = &buf[i];
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int idx = blockno % NCACHEHASHBUCKET;

  acquire(&bcache.buckets[idx].lock);

  // Is the block already cached?
  for(b = bcache.buckets[idx].head.next; b != &bcache.buckets[idx].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[idx].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached; recycle an unused buffer.
  // first find in its own bucket
  for(b = bcache.buckets[idx].head.prev; b != &bcache.buckets[idx].head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.buckets[idx].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.buckets[idx].lock);

  // There is a question about this part
  // Does it need to acquire the global lock?
  // steal from other buckets
  for (int i = 0; i < NCACHEHASHBUCKET; i++) {
    if (i == idx) continue;
    acquire(&bcache.buckets[i].lock);
    for(b = bcache.buckets[i].head.prev; b != &bcache.buckets[i].head; b = b->prev){
      if(b->refcnt == 0) {
        // move this buffer to the new bucket
        b->prev->next = b->next;
        b->next->prev = b->prev;
        release(&bcache.buckets[i].lock);

        acquire(&bcache.buckets[idx].lock);
        b->next = bcache.buckets[idx].head.next;
        b->prev = &bcache.buckets[idx].head;
        bcache.buckets[idx].head.next->prev = b;
        bcache.buckets[idx].head.next = b;
        
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&bcache.buckets[idx].lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.buckets[i].lock);
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

  int idx = b->blockno % NCACHEHASHBUCKET;
  acquire(&bcache.buckets[idx].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.buckets[idx].head.next;
    b->prev = &bcache.buckets[idx].head;
    bcache.buckets[idx].head.next->prev = b;
    bcache.buckets[idx].head.next = b;
  }
  
  release(&bcache.buckets[idx].lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


