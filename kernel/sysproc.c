/*===========================================================================
 * kernel/sysproc.c — 进程管理相关系统调用实现
 *
 * 实现 SYS_exit, SYS_fork, SYS_wait, SYS_getpid,
 *       SYS_kill, SYS_sleep, SYS_exec, SYS_sbrk,
 *       SYS_clone（批次6占位）
 *
 * 参考 xv6 sysproc.c，适配 x86-64
 *===========================================================================*/

#include <types.h>
#include <defs.h>
#include <param.h>
#include <idt.h>
#include <proc.h>
#include <syscall.h>
#include <libc.h>

/*---------------------------------------------------------------------------
 * sys_exit — 终止当前进程
 * 参数：arg1 = 退出码（暂时忽略，exit() 不接受参数）
 *---------------------------------------------------------------------------*/
int64_t
sys_exit(struct trapframe *tf)
{
    (void)tf;
    exit();
    return 0;   /* 不会到达 */
}

/*---------------------------------------------------------------------------
 * sys_fork — 创建子进程
 * 父进程返回子 PID，子进程返回 0
 *---------------------------------------------------------------------------*/
int64_t
sys_fork(struct trapframe *tf)
{
    (void)tf;
    return (int64_t)fork();
}

/*---------------------------------------------------------------------------
 * sys_wait — 等待子进程退出
 * 返回子 PID，若无子进程返回 -1
 *---------------------------------------------------------------------------*/
int64_t
sys_wait(struct trapframe *tf)
{
    (void)tf;
    return (int64_t)wait();
}

/*---------------------------------------------------------------------------
 * sys_getpid — 获取当前进程 PID
 *---------------------------------------------------------------------------*/
int64_t
sys_getpid(struct trapframe *tf)
{
    (void)tf;
    return (int64_t)myproc()->pid;
}

/*---------------------------------------------------------------------------
 * sys_kill — 向指定进程发送终止信号
 * 参数：arg1 = pid
 *---------------------------------------------------------------------------*/
int64_t
sys_kill(struct trapframe *tf)
{
    int pid;
    if (argint(tf, 1, &pid) < 0)
        return -1;
    return (int64_t)kill(pid);
}

/*---------------------------------------------------------------------------
 * sys_sleep — 睡眠指定时钟节拍数
 * 参数：arg1 = ticks（时钟中断次数）
 *
 * 使用全局 ticks 计数器（由 IRQ_TIMER 中断递增）实现定时睡眠。
 *---------------------------------------------------------------------------*/

/* 全局时钟节拍计数器（由 trap_handler 的 IRQ_TIMER 分支递增）*/
uint64_t ticks;
struct spinlock tickslock;

int64_t
sys_sleep(struct trapframe *tf)
{
    int n;
    if (argint(tf, 1, &n) < 0)
        return -1;

    acquire(&tickslock);
    uint64_t ticks0 = ticks;
    while (ticks - ticks0 < (uint64_t)n) {
        if (myproc()->killed) {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

/*---------------------------------------------------------------------------
 * sys_exec — 用新程序替换当前进程
 * 参数：arg1 = path（用户态字符串），arg2 = argv（用户态指针数组）
 *---------------------------------------------------------------------------*/
int64_t
sys_exec(struct trapframe *tf)
{
    char path[128];
    uint64_t uargv;

    if (argstr(tf, 1, path, sizeof(path)) < 0 || argptr(tf, 2, &uargv) < 0)
        return -1;

    /* 从用户地址空间拷贝 argv 指针数组 */
    char *argv[MAXARG];
    char argbuf[MAXARG][64];  /* 参数字符串缓冲区 */
    int i;

    struct proc *p = myproc();
    for (i = 0; i < MAXARG; i++) {
        uint64_t uarg;
        /* 从用户空间读取 argv[i] 指针 */
        if (copyin(p->pgdir, uargv + i * sizeof(uint64_t), &uarg, sizeof(uint64_t)) < 0)
            return -1;
        if (uarg == 0) {
            argv[i] = 0;
            break;
        }
        /* 拷贝字符串 */
        if (copyinstr(p->pgdir, uarg, argbuf[i], sizeof(argbuf[i])) < 0)
            return -1;
        argv[i] = argbuf[i];
    }
    if (i >= MAXARG)
        return -1;

    return exec(path, argv);
}

/*---------------------------------------------------------------------------
 * sys_sbrk — 调整进程数据段大小（brk/sbrk 系统调用）
 * 参数：arg1 = n（字节数，正数扩大，负数缩小）
 * 返回：扩大前的 sz（旧的堆顶地址），失败返回 -1
 *---------------------------------------------------------------------------*/
int64_t
sys_sbrk(struct trapframe *tf)
{
    int n;
    if (argint(tf, 1, &n) < 0)
        return -1;

    struct proc *p = myproc();
    uint64_t addr = p->sz;
    if (growproc(n) < 0)
        return -1;
    return (int64_t)addr;
}

/*---------------------------------------------------------------------------
 * sys_clone — 创建共享地址空间的线程
 * 参数：arg1 = fn（函数地址）, arg2 = stack（栈底）,
 *        arg3 = stacksz（栈大小）, arg4 = arg（传给 fn 的参数）
 *---------------------------------------------------------------------------*/
int64_t
sys_clone(struct trapframe *tf)
{
    uint64_t fn, stack, stacksz, arg;

    if (arguint64(tf, 1, &fn)      < 0 ||
        arguint64(tf, 2, &stack)   < 0 ||
        arguint64(tf, 3, &stacksz) < 0 ||
        arguint64(tf, 4, &arg)     < 0)
        return -1;

    return (int64_t)clone(fn, stack, stacksz, arg);
}
