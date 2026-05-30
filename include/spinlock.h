#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include <types.h>

/* 自旋锁结构体 */
struct spinlock {
    uint32_t    locked;      /* 是否被持有 (0=未持有, 1=持有) */
    char       *name;        /* 锁名称 (调试用) */
    struct cpu *cpu;         /* 持有此锁的 CPU */
};

void initlock(struct spinlock *lk, char *name);
void acquire(struct spinlock *lk);
void release(struct spinlock *lk);
int  holding(struct spinlock *lk);
void pushcli(void);
void popcli(void);

#endif /* __SPINLOCK_H__ */