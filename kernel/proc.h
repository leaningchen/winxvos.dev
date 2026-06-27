#ifndef __PROC_H__
#define __PROC_H__

#include <types.h>
#include <param.h>
#include <spinlock.h>
#include <idt.h>

/*---------------------------------------------------------------------------
 * 进程状态枚举 (参考 xv6 proc.h)
 *---------------------------------------------------------------------------*/
enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

/*---------------------------------------------------------------------------
 * 上下文保存 — 用于内核上下文切换
 * 只保存 callee-saved 寄存器 (r12-r15, rbx, rbp, rip)
 * 参考 xv6 的 struct context，适配64位
 *---------------------------------------------------------------------------*/
struct context {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rip;
};

/*---------------------------------------------------------------------------
 * 进程结构体 — 参考 xv6 的 struct proc，适配64位
 *---------------------------------------------------------------------------*/
struct proc {
    uint64_t          sz;          /* 进程内存大小 */
    pte_t            *pgdir;       /* 页表指针 (PML4) */
    char             *kstack;      /* 内核栈底 */
    enum procstate    state;       /* 进程状态 */
    int               pid;         /* 进程 ID */
    struct proc      *parent;      /* 父进程 */
    struct trapframe *tf;          /* 陷阱帧指针 */
    struct context    *context;    /* 切换上下文指针 */
    void             *chan;        /* 等待通道 (sleep/wakeup) */
    int               killed;      /* 是否被杀 */
    char              name[16];    /* 进程名 (调试用) */
};

/* 进程表与锁 */
extern struct proc ptable[NPROC];
extern struct spinlock ptable_lock;

#endif /* __PROC_H__ */