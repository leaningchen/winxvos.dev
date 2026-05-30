/*===========================================================================
 * kernel/bio.c — 块缓冲区缓存（Buffer Cache）
 *
 * 为磁盘块提供内存缓存，减少磁盘 I/O 并提供多进程同步点。
 *
 * 接口：
 *   bread(dev, blockno)  — 返回指定块的已锁定缓冲区（必要时从磁盘读取）
 *   bwrite(b)            — 将缓冲区写入磁盘（需持有 sleeplock）
 *   brelse(b)            — 释放缓冲区锁，移到 LRU 头部
 *
 * 实现：
 *   LRU 双向链表，head.next = 最近使用，head.prev = 最久未使用
 *   bcache.lock（自旋锁）保护链表结构和 refcnt
 *   b->lock（睡眠锁）保护缓冲区内容，允许持有期间睡眠
 *
 * 参考 xv6 bio.c
 *===========================================================================*/

#include <types.h>
#include <defs.h>
#include <param.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <fs.h>
#include <buf.h>

/*---------------------------------------------------------------------------
 * 全局缓冲区缓存
 *---------------------------------------------------------------------------*/
static struct {
    struct spinlock lock;       /* 保护链表结构和 refcnt */
    struct buf      buf[NBUF];  /* 缓冲区池 */
    struct buf      head;       /* LRU 链表哨兵节点 */
} bcache;

/*---------------------------------------------------------------------------
 * binit — 初始化缓冲区缓存
 * 建立循环双向链表，并初始化每个缓冲区的睡眠锁
 *---------------------------------------------------------------------------*/
void
binit(void)
{
    struct buf *b;

    initlock(&bcache.lock, "bcache");

    /* 建立循环 LRU 链表，head.next 为最近使用端 */
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;

    for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        initsleeplock(&b->lock, "buffer");
        bcache.head.next->prev = b;
        bcache.head.next       = b;
    }
}

/*---------------------------------------------------------------------------
 * bget — 获取指定设备块的缓冲区（内部函数）
 *
 * 先在缓存中查找，命中则增加 refcnt 并返回已锁定缓冲区。
 * 未命中则从 LRU 尾部淘汰一个空闲（refcnt==0 且非 dirty）缓冲区。
 *---------------------------------------------------------------------------*/
static struct buf *
bget(uint32_t dev, uint32_t blockno)
{
    struct buf *b;

    acquire(&bcache.lock);

    /* 查找缓存命中（从最近使用端开始）*/
    for (b = bcache.head.next; b != &bcache.head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    /* 缓存未命中：从 LRU 尾部淘汰空闲缓冲区 */
    for (b = bcache.head.prev; b != &bcache.head; b = b->prev) {
        if (b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
            b->dev     = dev;
            b->blockno = blockno;
            b->flags   = 0;      /* 清除 B_VALID，需要从磁盘读取 */
            b->refcnt  = 1;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    panic("bget: no buffers");
    return 0;   /* unreachable，消除编译器警告 */
}

/*---------------------------------------------------------------------------
 * bread — 返回指定块的已锁定、有效缓冲区
 * 若缓存中没有有效数据，从磁盘读取
 *---------------------------------------------------------------------------*/
struct buf *
bread(uint32_t dev, uint32_t blockno)
{
    struct buf *b = bget(dev, blockno);
    if ((b->flags & B_VALID) == 0)
        iderw(b);   /* 从磁盘读取 */
    return b;
}

/*---------------------------------------------------------------------------
 * bwrite — 将缓冲区内容写入磁盘（通过日志层调用 iderw）
 * 调用者必须持有 b->lock
 *---------------------------------------------------------------------------*/
void
bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    b->flags |= B_DIRTY;
    iderw(b);
}

/*---------------------------------------------------------------------------
 * brelse — 释放缓冲区，将其移到 LRU 最近使用端
 * 调用者必须持有 b->lock
 *---------------------------------------------------------------------------*/
void
brelse(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);

    acquire(&bcache.lock);
    b->refcnt--;
    if (b->refcnt == 0) {
        /* 没有其他进程等待此缓冲区，移到 LRU 最近使用端（head.next）*/
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next             = bcache.head.next;
        b->prev             = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next       = b;
    }
    release(&bcache.lock);
}
