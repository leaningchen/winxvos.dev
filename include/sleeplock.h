#ifndef __SLEEPLOCK_H__
#define __SLEEPLOCK_H__

#include <types.h>
#include <spinlock.h>

/*---------------------------------------------------------------------------
 * 睡眠锁（sleeplock）— 基于睡眠/唤醒的互斥锁
 *
 * 与自旋锁的区别：持有睡眠锁时进程可以睡眠（让出 CPU），
 * 适用于临界区较长的场景（如磁盘 I/O），
 * 而自旋锁的临界区必须不能睡眠。
 *
 * 参考 xv6 sleeplock.h
 *---------------------------------------------------------------------------*/
struct sleeplock {
    uint32_t      locked;   /* 是否被持有（0=未持有，1=持有）*/
    struct spinlock lk;     /* 保护 locked 字段本身的自旋锁 */
    char         *name;     /* 锁名称（调试用）*/
    int           pid;      /* 持有者进程 PID（调试用）*/
};

/* 接口函数（在 kernel/sleeplock.c 中实现）*/
void initsleeplock(struct sleeplock *lk, char *name);
void acquiresleep(struct sleeplock *lk);
void releasesleep(struct sleeplock *lk);
int  holdingsleep(struct sleeplock *lk);

#endif /* __SLEEPLOCK_H__ */
