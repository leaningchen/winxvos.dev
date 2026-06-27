#include <types.h>
#include <param.h>
#include <x86_64.h>
#include <spinlock.h>
#include <cpu.h>
#include <assert.h>

/*---------------------------------------------------------------------------
 * initlock — 初始化自旋锁
 *---------------------------------------------------------------------------*/
void initlock(struct spinlock *lk, char *name)
{
    lk->name   = name;
    lk->locked = 0;
    lk->cpu    = NULL;
}

/*---------------------------------------------------------------------------
 * holding — 检查当前 CPU 是否持有此锁
 *---------------------------------------------------------------------------*/
int holding(struct spinlock *lk)
{
    int r;
    pushcli();
    r = lk->locked && lk->cpu == mycpu();
    popcli();
    return r;
}

/*---------------------------------------------------------------------------
 * acquire — 获取自旋锁
 * 使用 xchg32 原子交换实现自旋等待
 *---------------------------------------------------------------------------*/
void acquire(struct spinlock *lk)
{
    pushcli();
    if (holding(lk))
        panic("acquire: already holding lock");

    while (xchg32(&lk->locked, 1) != 0)
        ;

    __sync_synchronize();

    lk->cpu = mycpu();
}

/*---------------------------------------------------------------------------
 * release — 释放自旋锁
 *---------------------------------------------------------------------------*/
void release(struct spinlock *lk)
{
    if (!holding(lk))
        panic("release: not holding lock");

    lk->cpu = NULL;

    __sync_synchronize();

    __asm__ volatile("movl $0, %0" : "+m" (lk->locked) : );

    popcli();
}

/*---------------------------------------------------------------------------
 * pushcli / popcli — 中断禁用/启用管理
 *---------------------------------------------------------------------------*/
void pushcli(void)
{
    uint64_t rflags = read_rflags();
    cli();
    if (mycpu()->ncli == 0)
        mycpu()->intena = (rflags & FL_IF) ? 1 : 0;
    mycpu()->ncli++;
}

void popcli(void)
{
    if (read_rflags() & FL_IF)
        panic("popcli: interruptible");
    struct cpu *c = mycpu();
    if (--c->ncli < 0)
        panic("popcli: ncli < 0");
    if (c->ncli == 0 && c->intena)
        sti();
}