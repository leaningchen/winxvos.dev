#ifndef __DEFS_H__
#define __DEFS_H__

#include <types.h>
#include <boot_info.h>

/* 前置类型声明 (避免头文件循环依赖) */
struct spinlock;
struct trapframe;
struct context;
struct sleeplock;
struct cpu;
struct proc;
struct inode;
struct file;
struct pipe;
struct stat;
struct superblock;
typedef uint64_t pte_t;
typedef uint64_t pml4e_t;
typedef struct ACPI_Header ACPI_Header;

/* kalloc.c */
void  kinit(BootInfo *info);
void *kalloc(void);
void  kfree(void *);

/* spinlock.c */
void initlock(struct spinlock *, char *);
void acquire(struct spinlock *);
void release(struct spinlock *);
int  holding(struct spinlock *);
void pushcli(void);
void popcli(void);

/* cpu.c */
int        cpuid(void);
struct cpu *mycpu(void);

/* idt.c */
void idt_init(void);
void idt_set_gate(int, void *, uint8_t, uint16_t);
void trap_handler(struct trapframe *);

/* panic.c */
void panic(const char *);

/* video.c */
void video_init(uint64_t, uint32_t, uint32_t, uint32_t, uint8_t);
void video_clear(uint32_t);
void video_print(const char *, uint32_t);
void video_print_at(int, int, const char *, uint32_t);

/* smp.c */
int  smp_init(BootInfo *);
int  smp_cpu_count(void);

/* acpi.c */
void         acpi_init(uint64_t);
ACPI_Header *acpi_find_table(const char *);
int          acpi_get_lapic_ids(uint8_t *, int);
uint64_t     acpi_get_lapic_base(void);

/* vm.c */
pte_t   *walkp4(pml4e_t *pml4, uint64_t va, int alloc);
int      mappages(pml4e_t *pml4, uint64_t va, uint64_t pa, uint64_t size, uint64_t perm);
pml4e_t *kvminit(void);
void     kvmswitch(void);
pml4e_t *uvmcreate(void);
void     uvmswitch(struct proc *p);
uint64_t uvmalloc(pml4e_t *pml4, uint64_t oldsz, uint64_t newsz);
uint64_t uvmdealloc(pml4e_t *pml4, uint64_t oldsz, uint64_t newsz);
int      uvmcopy(pml4e_t *old, pml4e_t *new, uint64_t sz);
void     uvmfree(pml4e_t *pml4, uint64_t sz);
void     uvmclear(pml4e_t *pml4, uint64_t va);
int      copyout(pml4e_t *pml4, uint64_t va, void *src, uint64_t len);
int      copyin(pml4e_t *pml4, uint64_t va, void *dst, uint64_t len);
int      copyinstr(pml4e_t *pml4, uint64_t va, char *dst, uint64_t max);

/* exec.c */
int  exec(char *path, char **argv);
int  loadseg(pml4e_t *pgdir, uint64_t va, struct inode *ip, uint32_t offset, uint32_t sz);

/* tss.c */
void     tss_init(void);
void     tss_set_rsp0(uint64_t rsp0);

/* proc.c */
void         pinit(void);
struct proc *myproc(void);
int          growproc(int n);
int          fork(void);
void         exit(void);
int          wait(void);
void         scheduler(void);
void         sched(void);
void         yield(void);
void         forkret(void);
void         sleep(void *chan, struct spinlock *lk);
void         wakeup(void *chan);
int          kill(int pid);
void         procdump(void);
void         userinit(void);
int          clone(uint64_t fn, uint64_t stack, uint64_t stacksz, uint64_t arg);

/* sleeplock.c */
void initsleeplock(struct sleeplock *lk, char *name);
void acquiresleep(struct sleeplock *lk);
void releasesleep(struct sleeplock *lk);
int  holdingsleep(struct sleeplock *lk);

/* swtch.S */
void swtch(struct context **old, struct context *new);

/* syscall.c */
void    syscall_init(void);
void    syscall_dispatch(struct trapframe *tf);
int     argint(struct trapframe *tf, int n, int *ip);
int     arguint64(struct trapframe *tf, int n, uint64_t *ip);
int     argptr(struct trapframe *tf, int n, uint64_t *pp);
int     argstr(struct trapframe *tf, int n, char *buf, int max);

/* vm.c (额外) */
extern uint64_t g_syscall_kstack;

/* sysproc.c — ticks 计数器（供 IRQ_TIMER 中断处理使用）*/
extern uint64_t        ticks;
extern struct spinlock tickslock;

/* bio.c */
void         binit(void);
struct buf  *bread(uint32_t, uint32_t);
void         bwrite(struct buf *);
void         brelse(struct buf *);

/* log.c */
void         initlog(int dev);
void         begin_op(void);
void         end_op(void);
void         log_write(struct buf *);

/* fs.c */
void          iinit(int dev);
void          readsb(int dev, struct superblock *sbp);
struct inode *ialloc(uint32_t dev, int16_t type);
void          iupdate(struct inode *);
struct inode *iget(uint32_t dev, uint32_t inum);
struct inode *idup(struct inode *);
void          ilock(struct inode *);
void          iunlock(struct inode *);
void          iput(struct inode *);
void          iunlockput(struct inode *);
void          itrunc(struct inode *);
void          stati(struct inode *, struct stat *);
int           readi(struct inode *, char *, uint32_t, uint32_t);
int           writei(struct inode *, char *, uint32_t, uint32_t);
struct inode *dirlookup(struct inode *, char *, uint32_t *);
int           dirlink(struct inode *, char *, uint32_t);
struct inode *namei(char *);
struct inode *nameiparent(char *, char *);

/* file.c */
void         fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *);
void         fileclose(struct file *);
int          filestat(struct file *, struct stat *);
int          fileread(struct file *, char *, int);
int          filewrite(struct file *, char *, int);

/* pipe.c */
int  pipealloc(struct file **, struct file **);
void pipeclose(struct pipe *, int);
int  piperead(struct pipe *, char *, int);
int  pipewrite(struct pipe *, char *, int);

/* ide.c */
void ideinit(void);
void ideintr(void);
void iderw(struct buf *);

#endif /* __DEFS_H__ */