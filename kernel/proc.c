/*===========================================================================
 * kernel/proc.c — 进程管理
 *
 * 实现进程生命周期管理：分配/释放、fork/exit/wait、调度器、
 * sleep/wakeup、kill 等。
 *
 * 参考 xv6 proc.c，适配 x86-64 四级页表和 64 位 ABI。
 *===========================================================================*/

#include <types.h>
#include <defs.h>
#include <param.h>
#include <memlayout.h>
#include <mmu.h>
#include <x86_64.h>
#include <spinlock.h>
#include <proc.h>
#include <cpu.h>
#include <idt.h>
#include <fs.h>
#include <sleeplock.h>
#include <file.h>
#include <libc.h>

/*---------------------------------------------------------------------------
 * 全局进程表（由 ptable_lock 保护）
 *---------------------------------------------------------------------------*/
struct proc        ptable[NPROC];
struct spinlock    ptable_lock;

/* init 进程指针（exit 时检查是否为 init 本身）*/
static struct proc *initproc;

/* 全局 PID 计数器（受 ptable_lock 保护，在 allocproc 中递增）*/
int nextpid = 1;

/* 前向声明 */
extern void forkret(void);
extern void trapret(void);
static void wakeup1(void *chan);

/*---------------------------------------------------------------------------
 * pinit — 初始化进程表锁
 * 由 kernel_main 在 idt_init 之前调用
 *---------------------------------------------------------------------------*/
void
pinit(void)
{
    initlock(&ptable_lock, "ptable");
}

/*---------------------------------------------------------------------------
 * myproc — 获取当前 CPU 上运行的进程指针
 * 禁用中断以防止在读取期间被重调度
 *---------------------------------------------------------------------------*/
struct proc *
myproc(void)
{
    struct cpu  *c;
    struct proc *p;
    pushcli();
    c = mycpu();
    p = c->proc;
    popcli();
    return p;
}

/*---------------------------------------------------------------------------
 * allocproc — 在进程表中找一个 UNUSED 槽位并初始化内核栈
 *
 * 内核栈布局（从底部 kstack 到顶部 kstack+KSTACKSIZE）：
 *
 *   kstack + KSTACKSIZE  <- 栈底（高地址）
 *   ┌──────────────────┐
 *   │  struct trapframe│ <- sp 先减 sizeof(trapframe)，p->tf 指向此处
 *   ├──────────────────┤
 *   │  trapret 地址    │ <- sp 再减 8，push 返回地址（forkret 结束后 ret 到 trapret）
 *   ├──────────────────┤
 *   │  struct context  │ <- sp 再减 sizeof(context)，p->context 指向此处
 *   │  (.rip=forkret)  │   context->rip = forkret
 *   └──────────────────┘
 *   kstack              <- 栈顶（低地址）
 *
 * 成功返回进程指针（状态 EMBRYO），失败返回 0。
 *---------------------------------------------------------------------------*/
static struct proc *
allocproc(void)
{
    struct proc *p;
    char        *sp;

    acquire(&ptable_lock);

    /* 寻找 UNUSED 槽位 */
    for (p = ptable; p < &ptable[NPROC]; p++)
        if (p->state == UNUSED)
            goto found;

    release(&ptable_lock);
    return 0;

found:
    p->state = EMBRYO;
    p->pid   = nextpid++;
    release(&ptable_lock);

    /* 分配内核栈（kalloc 返回内核虚拟地址，大小 PGSIZE = KSTACKSIZE）*/
    if ((p->kstack = kalloc()) == 0) {
        p->state = UNUSED;
        return 0;
    }
    sp = p->kstack + KSTACKSIZE;   /* sp 从栈底开始向下增长 */

    /* 1. 为 trapframe 留出空间（swtch->forkret->trapret 最终从这里 iretq）*/
    sp -= sizeof(*p->tf);
    p->tf = (struct trapframe *)sp;

    /* 2. 推入 trapret 的地址（forkret 函数返回时将 ret 到 trapret）*/
    sp -= sizeof(uint64_t);
    *(uint64_t *)sp = (uint64_t)trapret;

    /* 3. 为 context 留出空间，rip 设为 forkret */
    sp -= sizeof(*p->context);
    p->context       = (struct context *)sp;
    /* 清零所有寄存器字段 */
    p->context->r15  = 0;
    p->context->r14  = 0;
    p->context->r13  = 0;
    p->context->r12  = 0;
    p->context->rbx  = 0;
    p->context->rbp  = 0;
    p->context->rip  = (uint64_t)forkret;

    return p;
}

/*---------------------------------------------------------------------------
 * userinit — 创建第一个用户进程（init）
 *
 * 将 initcode（内嵌在内核镜像中的微型汇编程序）映射到进程地址空间
 * 的第 0 页，并设置好 trapframe，使进程从虚拟地址 0 开始执行。
 *
 * initcode 负责触发 exec("/init", ...) 系统调用，启动真正的 init。
 *---------------------------------------------------------------------------*/
void
userinit(void)
{
    struct proc *p;
    /* initcode.S 编译后由链接器以二进制形式嵌入内核 */
    extern char _binary_initcode_start[], _binary_initcode_size[];

    p = allocproc();
    initproc = p;

    /* 创建用户页表（包含内核映射）*/
    if ((p->pgdir = uvmcreate()) == 0)
        panic("userinit: out of memory");

    /* 将 initcode 加载到用户地址空间第 0 页 */
    uint64_t init_sz = (uint64_t)(uintptr_t)_binary_initcode_size;
    if (mappages(p->pgdir, 0, V2P((uint64_t)_binary_initcode_start),
                 init_sz, PTE_U | PTE_W) < 0)
        panic("userinit: mappages");
    p->sz = PGSIZE;   /* 用户地址空间大小为 1 页 */

    /* 初始化 trapframe，使 iretq 后跳到用户态 RIP=0 */
    p->tf->rip    = 0;                   /* initcode 入口 */
    p->tf->rsp    = PGSIZE;              /* 用户栈顶（1页底部）*/
    p->tf->cs     = USER_CODE_SEL;       /* 用户代码段 */
    p->tf->ss     = USER_DATA_SEL;       /* 用户栈段 */
    p->tf->rflags = FL_IF;               /* 使能中断 */

    /* 进程名（调试用）*/
    for (int i = 0; i < 8 && "initcode"[i]; i++)
        p->name[i] = "initcode"[i];

    /* 注意：cwd 在文件系统初始化后由 forkret 调用 iinit/initlog 设置 */

    /* 设为可运行 */
    acquire(&ptable_lock);
    p->state = RUNNABLE;
    release(&ptable_lock);
}

/*---------------------------------------------------------------------------
 * growproc — 增减当前进程的用户地址空间
 * n > 0：扩大 n 字节；n < 0：缩小 |n| 字节
 * 成功返回 0，失败返回 -1
 *---------------------------------------------------------------------------*/
int
growproc(int n)
{
    uint64_t     sz;
    struct proc *p = myproc();

    sz = p->sz;
    if (n > 0) {
        if ((sz = uvmalloc(p->pgdir, sz, sz + (uint64_t)n)) == 0)
            return -1;
    } else if (n < 0) {
        sz = uvmdealloc(p->pgdir, sz, sz + (uint64_t)n);
    }
    p->sz = sz;
    uvmswitch(p);
    return 0;
}

/*---------------------------------------------------------------------------
 * fork — 创建子进程
 *
 * 复制父进程的地址空间和 trapframe，子进程 fork 返回值为 0，
 * 父进程返回子进程 PID。
 *---------------------------------------------------------------------------*/
int
fork(void)
{
    int          pid;
    struct proc *np;
    struct proc *curproc = myproc();

    /* 分配新进程 */
    if ((np = allocproc()) == 0)
        return -1;

    /* 复制用户地址空间 */
    if (uvmcopy(curproc->pgdir, np->pgdir, curproc->sz) < 0) {
        kfree(np->kstack);
        np->kstack = 0;
        np->state  = UNUSED;
        return -1;
    }
    np->sz     = curproc->sz;
    np->parent = curproc;

    /* 复制 trapframe（子进程从同一个系统调用返回点继续执行）*/
    *np->tf = *curproc->tf;

    /* 子进程 fork 返回 0：将 rax 清零（syscall 约定返回值在 rax）*/
    np->tf->rax = 0;

    /* 复制进程名（调试用）*/
    for (int i = 0; i < 16; i++)
        np->name[i] = curproc->name[i];

    /* 复制文件描述符表 */
    for (int fd = 0; fd < NOFILE; fd++) {
        if (curproc->ofile[fd])
            np->ofile[fd] = filedup(curproc->ofile[fd]);
    }
    np->cwd = idup(curproc->cwd);

    pid = np->pid;

    acquire(&ptable_lock);
    np->state = RUNNABLE;
    release(&ptable_lock);

    return pid;
}

/*---------------------------------------------------------------------------
 * exit — 退出当前进程（不返回）
 *
 * 将进程设为 ZOMBIE，唤醒父进程（等待在 wait()），
 * 将孤儿子进程托付给 init，然后进入调度器。
 *---------------------------------------------------------------------------*/
void
exit(void)
{
    struct proc *curproc = myproc();
    struct proc *p;

    if (curproc == initproc)
        panic("init exiting");

    /* 关闭所有打开的文件 */
    for (int fd = 0; fd < NOFILE; fd++) {
        if (curproc->ofile[fd]) {
            fileclose(curproc->ofile[fd]);
            curproc->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(curproc->cwd);
    end_op();
    curproc->cwd = 0;

    acquire(&ptable_lock);

    /* 唤醒父进程（可能阻塞在 wait()）*/
    wakeup1(curproc->parent);

    /* 将孤儿子进程过继给 init */
    for (p = ptable; p < &ptable[NPROC]; p++) {
        if (p->parent == curproc) {
            p->parent = initproc;
            if (p->state == ZOMBIE)
                wakeup1(initproc);
        }
    }

    /* 进入 ZOMBIE 状态，等待父进程 wait() 回收 */
    curproc->state = ZOMBIE;
    sched();
    panic("zombie exit");   /* 不应到达此处 */
}

/*---------------------------------------------------------------------------
 * wait — 等待子进程退出
 *
 * 阻塞直到某个子进程变为 ZOMBIE，回收其资源并返回 PID。
 * 若无子进程，返回 -1。
 *---------------------------------------------------------------------------*/
int
wait(void)
{
    struct proc *p;
    int          havekids, pid;
    struct proc *curproc = myproc();

    acquire(&ptable_lock);
    for (;;) {
        havekids = 0;
        for (p = ptable; p < &ptable[NPROC]; p++) {
            if (p->parent != curproc)
                continue;
            havekids = 1;
            if (p->state == ZOMBIE) {
                /* 回收子进程资源 */
                pid = p->pid;
                kfree(p->kstack);
                p->kstack = 0;
                uvmfree(p->pgdir, p->sz);
                p->pgdir  = 0;
                p->pid    = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed  = 0;
                p->state   = UNUSED;
                release(&ptable_lock);
                return pid;
            }
        }

        /* 没有子进程，或当前进程已被 kill */
        if (!havekids || curproc->killed) {
            release(&ptable_lock);
            return -1;
        }

        /* 睡眠等待子进程变为 ZOMBIE（在 exit() 中被唤醒）*/
        sleep(curproc, &ptable_lock);
    }
}

/*---------------------------------------------------------------------------
 * scheduler — 每个 CPU 的进程调度器（永不返回）
 *
 * 循环遍历进程表，找到 RUNNABLE 进程后：
 *   1. 切换到该进程的页表（uvmswitch）
 *   2. swtch 切换到进程的内核上下文
 *   3. 进程运行完一个时间片后通过 yield/sleep/exit 返回 scheduler
 *   4. 切换回内核页表（kvmswitch）
 *---------------------------------------------------------------------------*/
void
scheduler(void)
{
    struct proc *p;
    struct cpu  *c = mycpu();
    c->proc = 0;

    for (;;) {
        /* 允许中断（防止没有可运行进程时 CPU 死锁）*/
        sti();

        acquire(&ptable_lock);
        for (p = ptable; p < &ptable[NPROC]; p++) {
            if (p->state != RUNNABLE)
                continue;

            /* 切换到该进程 */
            c->proc = p;
            uvmswitch(p);   /* 切换到进程页表并更新 TSS.RSP0 */
            p->state = RUNNING;

            /* 上下文切换：scheduler <-> 进程内核上下文 */
            swtch(&c->scheduler, p->context);

            /* 进程主动让出 CPU，回到内核页表 */
            kvmswitch();
            c->proc = 0;
        }
        release(&ptable_lock);
    }
}

/*---------------------------------------------------------------------------
 * sched — 从进程内核上下文切回 scheduler
 *
 * 调用者必须：
 *   1. 持有 ptable_lock（且只持有这一把锁）
 *   2. 已修改 p->state（非 RUNNING）
 *   3. 中断已禁用
 *---------------------------------------------------------------------------*/
void
sched(void)
{
    int          intena;
    struct proc *p = myproc();

    if (!holding(&ptable_lock))
        panic("sched ptable.lock");
    if (mycpu()->ncli != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched running");
    if (read_rflags() & FL_IF)
        panic("sched interruptible");

    intena = mycpu()->intena;
    swtch(&p->context, mycpu()->scheduler);
    mycpu()->intena = intena;
}

/*---------------------------------------------------------------------------
 * yield — 主动让出 CPU（用于时间片到期等）
 *---------------------------------------------------------------------------*/
void
yield(void)
{
    acquire(&ptable_lock);
    myproc()->state = RUNNABLE;
    sched();
    release(&ptable_lock);
}

/*---------------------------------------------------------------------------
 * forkret — fork 子进程第一次被调度时的入口
 *
 * 此时仍持有 ptable_lock（由 scheduler 持有），先释放锁。
 * 若为系统中的第一个进程（init），还需初始化文件系统。
 * 函数返回后由 ret 跳到 trapret（在 allocproc 中推入栈的返回地址）。
 *---------------------------------------------------------------------------*/
void
forkret(void)
{
    static int first = 1;

    /* 释放 scheduler 中持有的 ptable_lock */
    release(&ptable_lock);

    if (first) {
        /* 第一个进程（init）在文件系统初始化后设置 cwd = 根目录 */
        first = 0;
        myproc()->cwd = namei("/");
    }

    /* 函数返回后 ret 弹出 trapret 地址，执行 iretq 进入用户态 */
}

/*---------------------------------------------------------------------------
 * sleep — 原子释放锁并使当前进程睡眠在 chan 上
 *
 * 等价于：释放 lk → 睡眠 → 被 wakeup 唤醒后重新获取 lk
 * 此操作是原子的（通过先获取 ptable_lock 再释放 lk 实现），
 * 防止在释放 lk 和修改状态之间发生 wakeup 丢失。
 *---------------------------------------------------------------------------*/
void
sleep(void *chan, struct spinlock *lk)
{
    struct proc *p = myproc();

    if (p == 0)
        panic("sleep");
    if (lk == 0)
        panic("sleep without lk");

    /* 先获取 ptable_lock，再释放 lk，保证 wakeup 不会丢失 */
    if (lk != &ptable_lock) {
        acquire(&ptable_lock);
        release(lk);
    }

    /* 设置等待通道并睡眠 */
    p->chan  = chan;
    p->state = SLEEPING;
    sched();

    /* 被唤醒后清理通道 */
    p->chan = 0;

    /* 重新获取调用者的锁 */
    if (lk != &ptable_lock) {
        release(&ptable_lock);
        acquire(lk);
    }
}

/*---------------------------------------------------------------------------
 * wakeup1 — 唤醒所有在 chan 上睡眠的进程（内部版本，需持有 ptable_lock）
 *---------------------------------------------------------------------------*/
static void
wakeup1(void *chan)
{
    struct proc *p;
    for (p = ptable; p < &ptable[NPROC]; p++)
        if (p->state == SLEEPING && p->chan == chan)
            p->state = RUNNABLE;
}

/*---------------------------------------------------------------------------
 * wakeup — 唤醒所有在 chan 上睡眠的进程（外部接口，自动加锁）
 *---------------------------------------------------------------------------*/
void
wakeup(void *chan)
{
    acquire(&ptable_lock);
    wakeup1(chan);
    release(&ptable_lock);
}

/*---------------------------------------------------------------------------
 * kill — 向进程 pid 发送终止信号
 *
 * 设置 p->killed 标志；若进程正在睡眠则唤醒它，
 * 使其在下次从内核返回用户态时（trap.c 中）自行调用 exit()。
 *---------------------------------------------------------------------------*/
int
kill(int pid)
{
    struct proc *p;

    acquire(&ptable_lock);
    for (p = ptable; p < &ptable[NPROC]; p++) {
        if (p->pid == pid) {
            p->killed = 1;
            /* 若正在睡眠，唤醒使其能够响应 kill */
            if (p->state == SLEEPING)
                p->state = RUNNABLE;
            release(&ptable_lock);
            return 0;
        }
    }
    release(&ptable_lock);
    return -1;
}

/*---------------------------------------------------------------------------
 * procdump — 打印进程表摘要（调试用，在 panic 或 ^P 时调用）
 *---------------------------------------------------------------------------*/
void
procdump(void)
{
    static const char *states[] = {
        [UNUSED]   = "unused ",
        [EMBRYO]   = "embryo ",
        [SLEEPING] = "sleep  ",
        [RUNNABLE] = "runble ",
        [RUNNING]  = "run    ",
        [ZOMBIE]   = "zombie ",
    };
    struct proc       *p;
    const char        *state;

    for (p = ptable; p < &ptable[NPROC]; p++) {
        if (p->state == UNUSED)
            continue;
        if (p->state < (int)(sizeof(states)/sizeof(states[0])) &&
            states[p->state])
            state = states[p->state];
        else
            state = "???    ";
        kprintf("%d %s %s\n", p->pid, state, p->name);
    }
}

/*---------------------------------------------------------------------------
 * clone — 创建共享地址空间的线程（批次6）
 *
 * 与 fork 的区别：
 *   - 不复制地址空间（与父进程共享 pgdir）
 *   - 使用调用者提供的用户栈（stack + stacksz 作为初始 rsp）
 *   - 子线程从 fn(arg) 开始执行
 *
 * 参数：
 *   fn      — 线程入口函数（用户态虚拟地址）
 *   stack   — 用户栈底（已分配，stacksz 字节）
 *   stacksz — 栈大小（字节）
 *   arg     — 传给 fn 的参数（通过 rdi 传递）
 *
 * 成功返回子线程 PID，失败返回 -1。
 *
 * 约定：子线程运行时 rip=fn, rsp=stack+stacksz（栈顶），rdi=arg。
 *---------------------------------------------------------------------------*/
int
clone(uint64_t fn, uint64_t stack, uint64_t stacksz, uint64_t arg)
{
    struct proc *np;
    struct proc *curproc = myproc();

    if ((np = allocproc()) == 0)
        return -1;

    /* 共享父进程的页表（不复制地址空间）*/
    np->pgdir = curproc->pgdir;
    np->sz    = curproc->sz;
    np->parent = curproc;

    /* 复制 trapframe，然后修改入口和栈 */
    *np->tf = *curproc->tf;
    np->tf->rip = fn;
    np->tf->rsp = stack + stacksz;   /* 栈向低地址增长，从栈顶开始 */
    np->tf->rdi = arg;               /* x86-64 第一个参数 */
    np->tf->rax = 0;                 /* clone 在子线程中返回 0（可选）*/

    /* 共享文件描述符表 */
    for (int fd = 0; fd < NOFILE; fd++) {
        if (curproc->ofile[fd])
            np->ofile[fd] = filedup(curproc->ofile[fd]);
    }
    np->cwd = idup(curproc->cwd);

    /* 复制进程名，加 _t 后缀以区分线程 */
    strncpy(np->name, curproc->name, sizeof(np->name) - 3);
    int namelen = strlen(np->name);
    if (namelen < (int)sizeof(np->name) - 3) {
        np->name[namelen]   = '_';
        np->name[namelen+1] = 't';
        np->name[namelen+2] = 0;
    }

    int pid = np->pid;

    acquire(&ptable_lock);
    np->state = RUNNABLE;
    release(&ptable_lock);

    return pid;
}
