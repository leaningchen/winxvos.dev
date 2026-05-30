#ifndef __PROC_H__
#define __PROC_H__

#include <types.h>
#include <param.h>
#include <spinlock.h>
#include <idt.h>
#include <mmu.h>

/* 前置声明（避免循环依赖）*/
struct file;
struct inode;

/*---------------------------------------------------------------------------
 * 进程状态枚举 (参考 xv6 proc.h)
 *---------------------------------------------------------------------------*/
enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

/*---------------------------------------------------------------------------
 * 上下文保存 — 用于内核上下文切换
 * 只保存 callee-saved 寄存器 (r12-r15, rbx, rbp, rip)
 * 参考 xv6 的 struct context，适配64位
 *
 * 注意：push 顺序与 swtch.S 中的 pushq 顺序相同（r12在栈顶，rip在最低）
 *---------------------------------------------------------------------------*/
struct context {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rip;   /* 切换返回地址，对应 swtch 的 ret 目标 */
};

/*---------------------------------------------------------------------------
 * 进程结构体 — 参考 xv6 的 struct proc，适配64位
 *---------------------------------------------------------------------------*/
struct proc {
    uint64_t          sz;          /* 进程用户地址空间大小（字节）*/
    pml4e_t          *pgdir;       /* 四级页表根（PML4）内核虚拟地址 */
    char             *kstack;      /* 内核栈底（由 kalloc 分配，大小 KSTACKSIZE）*/
    enum procstate    state;       /* 进程状态 */
    int               pid;         /* 进程 ID */
    struct proc      *parent;      /* 父进程指针（wait/exit 使用）*/
    struct trapframe *tf;          /* 陷阱帧指针（位于 kstack 顶部）*/
    struct context   *context;     /* 上下文切换指针（位于 kstack 中）*/
    void             *chan;        /* 等待通道（sleep/wakeup 使用，NULL 表示未睡眠）*/
    int               killed;      /* 非 0 表示进程被杀死，下次 trap 返回时退出 */
    struct file      *ofile[NOFILE]; /* 打开的文件描述符表 */
    struct inode     *cwd;         /* 当前工作目录 inode */
    char              name[16];    /* 进程名（调试用）*/
};

/* 进程表与锁（在 proc.c 中定义，批次2实现）*/
extern struct proc ptable[NPROC];
extern struct spinlock ptable_lock;

#endif /* __PROC_H__ */