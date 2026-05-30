/*===========================================================================
 * user/usys.S — 用户态系统调用入口存根（x86-64 SYSCALL 指令）
 * 系统调用号与 include/syscall.h 完全一致
 *===========================================================================*/

    .text

#define SYSCALL(name, num)  \
    .globl name;            \
    name:                   \
        movq $num, %rax;    \
        syscall;            \
        ret

SYSCALL(exit,    1)
SYSCALL(fork,    2)
SYSCALL(wait,    3)
SYSCALL(getpid,  4)
SYSCALL(kill,    5)
SYSCALL(sleep,   6)
SYSCALL(exec,    7)
SYSCALL(sbrk,    8)
SYSCALL(open,    9)
SYSCALL(close,  10)
SYSCALL(read,   11)
SYSCALL(write,  12)
SYSCALL(unlink, 13)
SYSCALL(link,   14)
SYSCALL(mkdir,  15)
SYSCALL(chdir,  16)
SYSCALL(dup,    17)
SYSCALL(fstat,  18)
SYSCALL(mknod,  19)
SYSCALL(pipe,   20)
SYSCALL(clone,  21)
