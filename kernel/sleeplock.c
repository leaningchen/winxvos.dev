/*===========================================================================
 * kernel/sleeplock.c — 睡眠锁实现
 *
 * 睡眠锁在等待时让进程睡眠（调用 sleep），适合临界区较长的情况。
 * 内部用一个自旋锁保护 locked 字段，通过 sleep/wakeup 实现等待。
 *
 * 参考 xv6 sleeplock.c
 *===========================================================================*/

#include <types.h>
#include <defs.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <proc.h>

/*---------------------------------------------------------------------------
 * initsleeplock — 初始化睡眠锁
 *---------------------------------------------------------------------------*/
void
initsleeplock(struct sleeplock *lk, char *name)
{
    initlock(&lk->lk, "sleep lock");
    lk->name   = name;
    lk->locked = 0;
    lk->pid    = 0;
}

/*---------------------------------------------------------------------------
 * acquiresleep — 获取睡眠锁
 * 若锁已被持有则睡眠等待，直到持有者调用 releasesleep 唤醒
 *---------------------------------------------------------------------------*/
void
acquiresleep(struct sleeplock *lk)
{
    acquire(&lk->lk);
    while (lk->locked) {
        /* 在 lk->lk 自旋锁保护下睡眠，wakeup 时重新检查 */
        sleep(lk, &lk->lk);
    }
    lk->locked = 1;
    lk->pid    = myproc()->pid;
    release(&lk->lk);
}

/*---------------------------------------------------------------------------
 * releasesleep — 释放睡眠锁，并唤醒等待者
 *---------------------------------------------------------------------------*/
void
releasesleep(struct sleeplock *lk)
{
    acquire(&lk->lk);
    lk->locked = 0;
    lk->pid    = 0;
    wakeup(lk);
    release(&lk->lk);
}

/*---------------------------------------------------------------------------
 * holdingsleep — 判断当前进程是否持有此睡眠锁
 *---------------------------------------------------------------------------*/
int
holdingsleep(struct sleeplock *lk)
{
    int r;
    acquire(&lk->lk);
    r = lk->locked && (lk->pid == myproc()->pid);
    release(&lk->lk);
    return r;
}
