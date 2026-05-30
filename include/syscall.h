#ifndef __SYSCALL_H__
#define __SYSCALL_H__

/*===========================================================================
 * include/syscall.h — 系统调用号定义
 *
 * 用户态通过 syscall 指令触发内核服务，系统调用号放在 %rax。
 * 参数顺序遵循 Linux AMD64 ABI:
 *   arg1=%rdi, arg2=%rsi, arg3=%rdx, arg4=%r10, arg5=%r8, arg6=%r9
 * 返回值在 %rax（负值表示错误）。
 *===========================================================================*/

/* 进程管理 */
#define SYS_exit      1    /* void exit(int status) */
#define SYS_fork      2    /* int fork(void) */
#define SYS_wait      3    /* int wait(void) */
#define SYS_getpid    4    /* int getpid(void) */
#define SYS_kill      5    /* int kill(int pid) */
#define SYS_sleep     6    /* int sleep(int ticks) */
#define SYS_exec      7    /* int exec(char *path, char **argv) */
#define SYS_sbrk      8    /* void *sbrk(int n) */

/* 文件系统（批次5实现）*/
#define SYS_open      9    /* int open(char *path, int flags) */
#define SYS_close    10    /* int close(int fd) */
#define SYS_read     11    /* int read(int fd, void *buf, int n) */
#define SYS_write    12    /* int write(int fd, void *buf, int n) */
#define SYS_unlink   13    /* int unlink(char *path) */
#define SYS_link     14    /* int link(char *old, char *new) */
#define SYS_mkdir    15    /* int mkdir(char *path) */
#define SYS_chdir    16    /* int chdir(char *path) */
#define SYS_dup      17    /* int dup(int fd) */
#define SYS_fstat    18    /* int fstat(int fd, struct stat *st) */
#define SYS_mknod    19    /* int mknod(char *path, int major, int minor) */
#define SYS_pipe     20    /* int pipe(int pfd[2]) */

/* 线程支持（批次6实现）*/
#define SYS_clone    21    /* int clone(void *fn, void *stack, void *arg) */

/* 系统调用总数 */
#define NSYSCALLS    22

#endif /* __SYSCALL_H__ */
