/*===========================================================================
 * kernel/syscall.c — 系统调用分发器
 *
 * syscall_dispatch() 由 syscall_entry.S 调用，负责：
 *   1. 从 trapframe 读取系统调用号（rax）和参数
 *   2. 检查合法性
 *   3. 调用对应的系统调用处理函数
 *   4. 将返回值写回 trapframe->rax
 *
 * 参数传递约定（Linux AMD64 ABI）：
 *   arg1=%rdi, arg2=%rsi, arg3=%rdx, arg4=%r10, arg5=%r8, arg6=%r9
 *
 * 参考 xv6 syscall.c，适配 x86-64
 *===========================================================================*/

#include <types.h>
#include <defs.h>
#include <param.h>
#include <x86_64.h>
#include <idt.h>
#include <proc.h>
#include <syscall.h>
#include <libc.h>

/*---------------------------------------------------------------------------
 * 系统调用参数提取辅助函数
 * 直接从 trapframe 读取对应寄存器
 *---------------------------------------------------------------------------*/

/* 读取第 n 个系统调用参数（n=1..6）*/
static inline uint64_t
argraw(struct trapframe *tf, int n)
{
    switch (n) {
    case 1: return tf->rdi;
    case 2: return tf->rsi;
    case 3: return tf->rdx;
    case 4: return tf->r10;
    case 5: return tf->r8;
    case 6: return tf->r9;
    }
    panic("argraw: bad n");
    return -1;
}

/* 获取第 n 个 uint64_t 参数 */
int
argint(struct trapframe *tf, int n, int *ip)
{
    *ip = (int)argraw(tf, n);
    return 0;
}

int
arguint64(struct trapframe *tf, int n, uint64_t *ip)
{
    *ip = argraw(tf, n);
    return 0;
}

/* 获取第 n 个指针参数（用户态地址）*/
int
argptr(struct trapframe *tf, int n, uint64_t *pp)
{
    *pp = argraw(tf, n);
    return 0;
}

/* 获取第 n 个参数作为字符串（从用户地址空间复制）
 * 将用户态字符串复制到内核缓冲区 buf，最多 max 字节 */
int
argstr(struct trapframe *tf, int n, char *buf, int max)
{
    uint64_t addr = argraw(tf, n);
    struct proc *p = myproc();
    if (copyinstr(p->pgdir, addr, buf, (uint64_t)max) < 0)
        return -1;
    return 0;
}

/*---------------------------------------------------------------------------
 * 系统调用处理函数前向声明
 * sysproc.c 实现进程相关，sysfile.c 实现文件相关（批次5）
 *---------------------------------------------------------------------------*/

/* sysproc.c */
int64_t sys_exit(struct trapframe *tf);
int64_t sys_fork(struct trapframe *tf);
int64_t sys_wait(struct trapframe *tf);
int64_t sys_getpid(struct trapframe *tf);
int64_t sys_kill(struct trapframe *tf);
int64_t sys_sleep(struct trapframe *tf);
int64_t sys_exec(struct trapframe *tf);
int64_t sys_sbrk(struct trapframe *tf);

/* sysfile.c（批次5实现，当前为 stub）*/
int64_t sys_open(struct trapframe *tf);
int64_t sys_close(struct trapframe *tf);
int64_t sys_read(struct trapframe *tf);
int64_t sys_write(struct trapframe *tf);
int64_t sys_unlink(struct trapframe *tf);
int64_t sys_link(struct trapframe *tf);
int64_t sys_mkdir(struct trapframe *tf);
int64_t sys_chdir(struct trapframe *tf);
int64_t sys_dup(struct trapframe *tf);
int64_t sys_fstat(struct trapframe *tf);
int64_t sys_mknod(struct trapframe *tf);
int64_t sys_pipe(struct trapframe *tf);

/* sysproc.c（线程，批次6）*/
int64_t sys_clone(struct trapframe *tf);

/*---------------------------------------------------------------------------
 * 系统调用分发表
 *---------------------------------------------------------------------------*/
typedef int64_t (*syscall_fn_t)(struct trapframe *tf);

static syscall_fn_t syscall_table[NSYSCALLS] = {
    [SYS_exit]   = sys_exit,
    [SYS_fork]   = sys_fork,
    [SYS_wait]   = sys_wait,
    [SYS_getpid] = sys_getpid,
    [SYS_kill]   = sys_kill,
    [SYS_sleep]  = sys_sleep,
    [SYS_exec]   = sys_exec,
    [SYS_sbrk]   = sys_sbrk,
    [SYS_open]   = sys_open,
    [SYS_close]  = sys_close,
    [SYS_read]   = sys_read,
    [SYS_write]  = sys_write,
    [SYS_unlink] = sys_unlink,
    [SYS_link]   = sys_link,
    [SYS_mkdir]  = sys_mkdir,
    [SYS_chdir]  = sys_chdir,
    [SYS_dup]    = sys_dup,
    [SYS_fstat]  = sys_fstat,
    [SYS_mknod]  = sys_mknod,
    [SYS_pipe]   = sys_pipe,
    [SYS_clone]  = sys_clone,
};

/*---------------------------------------------------------------------------
 * syscall_dispatch — 系统调用分发入口（由 syscall_entry.S 调用）
 *
 * @tf: 指向内核栈上的 trapframe（包含系统调用号和参数）
 *---------------------------------------------------------------------------*/
void
syscall_dispatch(struct trapframe *tf)
{
    uint64_t num = tf->rax;   /* 系统调用号 */

    if (num == 0 || num >= NSYSCALLS || syscall_table[num] == 0) {
        kprintf("syscall: unknown syscall %lu\n", num);
        tf->rax = (uint64_t)-1;
        return;
    }

    /* 调用对应处理函数，返回值写入 rax（用户程序从 rax 读取返回值）*/
    tf->rax = (uint64_t)syscall_table[num](tf);
}

/*---------------------------------------------------------------------------
 * syscall_init — 配置 SYSCALL/SYSRET 所需 MSR
 *
 * STAR MSR 配置（与 tss.c 中 GDT 布局一致）：
 *   STAR[47:32] = KERN_CODE_SEL = 0x18  → syscall 时 CS=0x18, SS=0x20
 *   STAR[63:48] = 0x001B                → sysretq 时 CS=0x1B+16=0x2B, SS=0x1B+8=0x23
 *
 * MSR 地址：
 *   0xC0000080 = EFER  (Extended Feature Enable Register)
 *   0xC0000081 = STAR  (System Target Address Register)
 *   0xC0000082 = LSTAR (Long Mode STAR — 64位 SYSCALL 目标 RIP)
 *   0xC0000084 = SFMASK (SYSCALL Flag Mask — 进入时清除的 RFLAGS 位)
 *---------------------------------------------------------------------------*/
extern void syscall_entry(void);

void
syscall_init(void)
{
    /* 1. 启用 EFER.SCE（SYSCALL Enable 位）*/
    uint64_t efer = rdmsr(0xC0000080);
    wrmsr(0xC0000080, efer | 1);

    /* 2. 配置 STAR：高32位 = [63:48]=0x001B [47:32]=0x0018 */
    wrmsr(0xC0000081, 0x001B001800000000ULL);

    /* 3. 配置 LSTAR = syscall_entry 的地址 */
    wrmsr(0xC0000082, (uint64_t)syscall_entry);

    /* 4. 配置 SFMASK：syscall 时清除 IF（0x200）等位 */
    wrmsr(0xC0000084, 0x200ULL);
}
