# WinixOS 完整系统实现计划

## Context

WinixOS 目前是一个具备启动、VESA 图形输出、物理内存分配、IDT/中断处理、SMP 多核初始化的 x86-64 裸机内核，但缺少操作系统的核心组件：虚拟内存管理、进程管理与调度、系统调用接口、文件系统、磁盘驱动、用户态程序执行。用户希望将其发展为一个相对完整的系统，实现以下全部模块：

- 虚拟内存管理（x86-64 四级页表）
- 进程管理与调度（fork/exec/exit/wait + 时间片轮转）
- 线程支持（内核线程 / clone 系统调用）
- 系统调用接口（SYSCALL/SYSRET 指令）
- 文件系统（类 xv6 inode 文件系统）
- 磁盘驱动（ATA PIO IDE）
- 用户态程序（ELF 加载器 + 最简 shell）
- 管道 / IPC

实现参考：**xv6-public**（`D:\CYGWIN\MSYS64\home\leaningchen\studios\xv6-public`）架构移植到 64 位，同时参考 Linux / Minix 在部分细节上的更完整实现。所有代码须附有详细中文注释。

---

## 架构决策

### x86-64 vs xv6 i386 的关键差异

| 特性 | xv6 i386 | WinixOS x86-64 |
|------|-----------|----------------|
| 页表 | 二级（PD+PT） | 四级（PML4+PDPT+PD+PT） |
| 系统调用 | INT 0x40 + trapframe | SYSCALL/SYSRET + MSR |
| 上下文结构 | edi/esi/ebx/ebp/eip (5寄存器) | rbx/rbp/r12-r15/rip (7寄存器) |
| 内核映射基址 | KERNBASE = 0x80000000 | KERNBASE = 0xFFFF800000000000 |
| 用户地址空间 | 0 ~ KERNBASE | 0 ~ 0x0000800000000000 |
| TSS | 一个字段 | 64位 TSS，含 RSP0 |

### 内存布局（新设计）

```
虚拟地址空间 (x86-64, 48位):
┌─────────────────────────────────────────┐
│ 0x0000000000000000 ~ 0x00007FFFFFFFFFFF │  用户空间 (128TB)
│   0x400000: 用户程序起始地址             │
│   向上增长的堆                           │
│   向下增长的用户栈 (接近 0x800000000000)  │
├─────────────────────────────────────────┤
│ 规范空洞 (non-canonical)                 │
├─────────────────────────────────────────┤
│ 0xFFFF800000000000 ~ 0xFFFFFFFFFFFFFFFF │  内核空间 (128TB)
│   KERNBASE = 0xFFFF800000000000          │
│   物理内存直接映射区 (最大2GB)            │
│     KERNBASE+0 => 物理地址0              │
│     KERNBASE+PHYSTOP => 物理内存顶        │
│   内核代码/数据: 0xFFFF800000100000+     │
│   每进程内核栈: 由 kalloc 分配，映射到高地址 │
└─────────────────────────────────────────┘
```

---

## 实现阶段规划

### 阶段一：虚拟内存管理（基础）

**目标**：建立 x86-64 四级页表，实现内核高地址映射，为进程管理奠定基础。

#### 新增/修改文件

| 文件 | 类型 | 说明 |
|------|------|------|
| `include/mmu.h` | 新增 | x86-64 MMU 定义（页表项、PML4/PDPT/PD/PT 宏、CR3等）；不重复 param.h 已有的 PGSIZE |
| `include/memlayout.h` | 修改 | 新增 KERNBASE、V2P/P2V 宏、用户地址空间布局 |
| `include/tss.h` | 新增 | 64位 TSS 结构体 |
| `kernel/vm.c` | 新增 | 页表管理：kvminit/mappages/walkp4/kvmswitch/uvmcreate等 |
| `kernel/tss.c` | 新增 | TSS 初始化（加入 GDT），设置 RSP0 |
| `include/proc.h` | 修改 | `pgdir` 改为 `pml4e_t *` |
| `include/defs.h` | 修改 | 追加 vm.c 和 tss.c 的函数声明 |
| `linker/kernel.ld` | 修改 | 内核链接到 KERNBASE+0x100000 |
| `kernel/entry64.S` | 修改 | 启动时建立临时页表后跳转到高地址内核 |
| `kernel/main.c` | 修改 | 在 video_init 之前调用 kvminit() + kvmswitch() |

#### 关键实现

```c
// include/mmu.h 关键宏（参考 Linux arch/x86/include/asm/pgtable_types.h）
// 注意：PGSIZE 已在 param.h 中定义，不重复
#define PTE_P     (1ULL << 0)   // Present
#define PTE_W     (1ULL << 1)   // Writable
#define PTE_U     (1ULL << 2)   // User-accessible
#define PTE_PS    (1ULL << 7)   // Page Size (2MB 大页)
#define PTE_NX    (1ULL << 63)  // No-Execute

#define PXSHIFT(l) (12 + 9*(l))           // l=0:PT, 1:PD, 2:PDPT, 3:PML4
#define PX(l, va)  (((va) >> PXSHIFT(l)) & 0x1FFUL)
#define PTESZ      512          // 每级页表项数
typedef uint64_t pte_t;         // 兼容旧 proc.h 中的 pte_t
typedef uint64_t pml4e_t;       // PML4 表项类型别名

// 从 PTE 提取物理地址（去掉低12位标志）
#define PTE_ADDR(pte)   ((uint64_t)(pte) & ~0xFFFULL)
#define PTE_FLAGS(pte)  ((uint64_t)(pte) &  0xFFFULL)
```

```c
// include/memlayout.h 新增部分
#define KERNBASE  0xFFFF800000000000ULL   // 内核虚拟地址基址
#define V2P(va)   ((uint64_t)(va) - KERNBASE)
#define P2V(pa)   ((void *)((uint64_t)(pa) + KERNBASE))

// 用户地址空间顶部（留出内核部分）
#define USERTOP   0x0000800000000000ULL
// 用户栈顶（exec 时使用，在 USERTOP 下方）
#define USTACK    USERTOP
```

```c
// kernel/vm.c 核心函数
pte_t   *walkp4(pml4e_t *pml4, uint64_t va, int alloc); // 四级页表遍历，返回 PT 级 PTE 指针
int      mappages(pml4e_t *pml4, uint64_t va, uint64_t pa, uint64_t size, uint64_t perm);
pml4e_t *kvminit(void);     // 建立内核页表（物理内存直接映射 P2V 区）
void     kvmswitch(void);   // 切换到内核 PML4（写 CR3）
pml4e_t *uvmcreate(void);   // 创建空用户页表（内核部分用 kvminit 结果的高地址 entry 共享）
void     uvmswitch(struct proc *p); // 切换到进程页表
int      uvmcopy(pml4e_t *old, pml4e_t *new, uint64_t sz); // fork 时复制用户内存
void     uvmfree(pml4e_t *pml4, uint64_t sz); // 释放用户页表及物理页
int      copyout(pml4e_t *pml4, uint64_t va, void *src, uint64_t len);
int      copyin(pml4e_t *pml4, uint64_t va, void *dst, uint64_t len);
```

#### entry64.S 启动流程（关键变化）

当前 entry64.S 在物理地址 0x100000 运行，切换高地址后需要：

```asm
# 新增：在调用 kernel_main 之前建立临时页表
# 1. 分配 4 个静态页（PML4 + PDPT + PD 大页模式）存放于内核数据段
# 2. 建立两个映射：
#    - 恒等映射: VA[0x000000, 0x200000) → PA[0, 0x200000)（临时，允许 RIP 在低地址继续执行）
#    - 高地址映射: VA[KERNBASE, KERNBASE+PHYSTOP) → PA[0, PHYSTOP)（2MB 大页，PTE_PS）
# 3. 写 CR3，启用新页表
# 4. 用绝对跳转指令（movabs + jmp *reg）跳转到高地址的标签
# 5. 跳转后移除恒等映射（或保留，vmswitch 时由 kvminit 接管）
# 6. 更新 %rsp 到高地址
# 7. 调用 kernel_main（此时 RIP 已在高地址）

# 静态临时页表（在 .data 段）
.align 4096
entry_pml4: .space 4096    # PML4
entry_pdpt: .space 4096    # PDPT（低地址用）
entry_pd:   .space 4096    # PD（低地址用，2MB 页）
kentry_pdpt:.space 4096    # PDPT（高地址 KERNBASE 用）
kentry_pd:  .space 4096    # PD（高地址，2MB 页）
```

#### linker/kernel.ld 修改

```ld
KERNBASE = 0xFFFF800000000000;
. = KERNBASE + 0x100000;   /* 内核链接到高地址 */
```

同时需在 GDB/QEMU 调试时注意符号地址偏移。

---

### 阶段二：进程管理与调度

**目标**：实现完整进程生命周期管理和时间片轮转调度器。

#### 新增/修改文件

| 文件 | 类型 | 说明 |
|------|------|------|
| `include/proc.h` | 修改 | 更新为 64 位版本（context 用 rbx/rbp/r12-r15/rip） |
| `kernel/proc.c` | 新增 | 进程管理全部实现 |
| `kernel/swtch.S` | 新增 | x86-64 上下文切换（保存/恢复 7 个被调用者寄存器） |
| `kernel/sleeplock.c` | 新增 | 睡眠锁实现（基于 sleep/wakeup） |
| `include/sleeplock.h` | 新增 | 睡眠锁结构体 |
| `kernel/tss.c` | 新增 | 64位 TSS 初始化（设置 RSP0，内核栈切换） |
| `include/tss.h` | 新增 | TSS 结构体（16字节 aligned） |
| `include/param.h` | 修改 | 新增 KSTACKSIZE=4096、NOFILE=16、NFILE=100 等 |

#### 关键数据结构

```c
// include/proc.h
struct context {
    uint64_t rbx, rbp;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;   // 返回地址（切换后从此处继续）
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc {
    uint64_t          sz;           // 用户地址空间大小（字节）
    pml4e_t          *pgdir;        // PML4 页表物理地址
    char             *kstack;       // 内核栈（4KB，由 kalloc 分配）
    enum procstate    state;
    int               pid;
    struct proc      *parent;
    struct trapframe *tf;           // 陷阱帧指针（在 kstack 中）
    struct context   *context;      // 上下文指针（在 kstack 中）
    void             *chan;         // sleep 通道
    int               killed;
    struct file      *ofile[NOFILE];
    struct inode     *cwd;          // 当前目录 inode
    char              name[16];
};
```

```asm
# kernel/swtch.S (x86-64)
# void swtch(struct context **old, struct context *new)
swtch:
    # 保存被调用者寄存器到当前栈，更新 *old
    pushq %rbp; pushq %rbx; pushq %r15; pushq %r14
    pushq %r13; pushq %r12
    movq %rsp, (%rdi)   # *old = rsp
    # 切换到 new 栈，恢复寄存器
    movq %rsi, %rsp
    popq %r12; popq %r13; popq %r14; popq %r15
    popq %rbx; popq %rbp
    ret                 # 跳转到 new->rip
```

#### 关键函数

```c
// kernel/proc.c
void     pinit(void);              // 初始化进程表锁
void     userinit(void);           // 创建第一个用户进程（initcode）
int      fork(void);               // 复制进程（COW 可选）
void     exit(int status);         // 进程退出
int      wait(int *status);        // 等待子进程
void     scheduler(void) __attribute__((noreturn)); // 调度器主循环
void     sched(void);              // 进入调度器
void     yield(void);              // 主动让出 CPU
void     sleep(void *chan, struct spinlock *lk); // 睡眠
void     wakeup(void *chan);       // 唤醒
struct proc *myproc(void);         // 当前进程（通过 GS 寄存器）
```

#### TSS（任务状态段）

x86-64 中 SYSCALL/SYSRET 不自动切换栈，需要通过 TSS 的 RSP0 字段告知硬件内核栈地址：

```c
// kernel/tss.c
struct tss64 {
    uint32_t reserved0;
    uint64_t rsp[3];    // rsp0: 切入内核时的栈顶
    uint64_t ist[8];    // IST 栈（中断栈表）
    uint8_t  reserved1[10];
    uint16_t iomap_base;
} __attribute__((packed));

void tss_init(void);                    // 每 CPU 初始化一个 TSS
void tss_set_rsp0(uint64_t rsp0);       // 切换进程时更新 RSP0
```

---

### 阶段三：系统调用接口

**目标**：实现 SYSCALL/SYSRET 快速系统调用路径，支持完整系统调用分发。

#### 新增/修改文件

| 文件 | 类型 | 说明 |
|------|------|------|
| `include/syscall.h` | 新增 | 系统调用号定义（参考 Linux ABI） |
| `kernel/syscall.c` | 新增 | 系统调用分发器 + 参数获取辅助函数 |
| `kernel/sysproc.c` | 新增 | 进程类系统调用：fork/exit/wait/getpid/yield/sleep/kill/sbrk |
| `kernel/sysfile.c` | 新增 | 文件类系统调用：open/read/write/close/stat/mkdir/chdir/exec/pipe/dup |
| `kernel/syscall_entry.S` | 新增 | SYSCALL 入口汇编（MSR 配置 + 寄存器保存/恢复） |
| `kernel/idt.c` | 修改 | 保留 INT 路径作为回退，同时配置 SYSCALL MSR |

#### SYSCALL 入口设计

Linux x86-64 系统调用约定（System V ABI）：
- 系统调用号：`rax`
- 参数：`rdi, rsi, rdx, r10, r8, r9`
- 返回值：`rax`
- 用 `SYSCALL` 指令进入内核，`SYSRET` 返回

```asm
# kernel/syscall_entry.S
syscall_entry:
    swapgs                  # 切换 GS（用户 GS ↔ 内核 GS，保存用户栈指针）
    movq %rsp, %gs:CPU_USTACK  # 保存用户栈
    movq %gs:CPU_KSTACK, %rsp  # 切换到内核栈（TSS RSP0）
    # 构建 syscall_frame（保存 rcx/r11/全部调用者寄存器）
    ...
    call syscall_dispatch
    # 恢复寄存器，sysretq
```

```c
// include/syscall.h — 系统调用号（参考 Linux x86-64 ABI）
#define SYS_read     0
#define SYS_write    1
#define SYS_open     2
#define SYS_close    3
#define SYS_stat     4
#define SYS_fstat    5
#define SYS_lseek    8
#define SYS_mmap     9
#define SYS_brk      12
#define SYS_fork     57
#define SYS_exec     59
#define SYS_exit     60
#define SYS_wait4    61
#define SYS_getpid   39
#define SYS_yield    24   // sched_yield
#define SYS_mkdir    83
#define SYS_chdir    80
#define SYS_pipe     22
#define SYS_dup      32
#define SYS_dup2     33
#define SYS_kill     62
#define SYS_clone    56   // 线程支持
```

---

### 阶段四：磁盘驱动 + 文件系统

**目标**：实现 ATA PIO IDE 驱动 + 类 xv6 的五层文件系统（磁盘块缓存 → 日志 → inode → 目录 → 路径）。

#### 新增/修改文件

| 文件 | 类型 | 说明 |
|------|------|------|
| `include/buf.h` | 新增 | 磁盘块缓冲区结构（含 sleeplock） |
| `include/fs.h` | 新增 | 磁盘格式：超级块、dinode、dirent |
| `include/file.h` | 新增 | 内存文件结构、inode、devsw |
| `include/stat.h` | 新增 | 文件状态结构 |
| `kernel/ide.c` | 新增 | ATA PIO IDE 驱动（主从盘，IRQ 14） |
| `kernel/bio.c` | 新增 | 块缓存（LRU 双链表，30块缓冲） |
| `kernel/log.c` | 新增 | 日志层（物理重做日志，崩溃恢复） |
| `kernel/fs.c` | 新增 | inode 层、目录操作、路径解析 |
| `kernel/file.c` | 新增 | 文件描述符管理（filealloc/fileread/filewrite） |
| `kernel/pipe.c` | 新增 | 管道实现（基于内核缓冲区 + sleep/wakeup） |
| `tools/mkfs.c` | 新增 | 制作文件系统镜像工具（生成 fs.img） |
| `Makefile` | 修改 | 新增 fs.img 生成目标，挂载为第二个磁盘 |

#### 磁盘布局（参考 xv6）

```
fs.img 磁盘布局 (512字节/块):
[ boot(0) | superblock(1) | log(2..31) | inodes(32..57) | bitmap(58) | data(59..) ]

超级块内容:
  magic    = 0x10203040
  size     = 总块数
  nblocks  = 数据块数
  ninodes  = inode 数 (200)
  nlog     = 日志块数 (30)
  logstart = 2
  inodestart = 32
  bmapstart  = 58
```

#### 文件系统五层架构

```
层5: 路径层      namei(), nameiparent()
层4: 目录层      dirlookup(), dirlink()
层3: inode层     ialloc(), ilock(), readi(), writei(), itrunc()
层2: 日志层      begin_op(), end_op(), log_write()
层1: 块缓存层    bread(), bwrite(), brelse()
层0: 磁盘驱动    iderw() (IDE PIO)
```

---

### 阶段五：线程支持

**目标**：实现内核线程和 `clone()` 系统调用，支持共享地址空间的轻量级线程。

#### 实现方式（参考 Linux clone + xv6 proc）

```c
// clone 系统调用
// flags: CLONE_VM（共享地址空间）| CLONE_FILES（共享文件描述符）
int clone(int (*fn)(void *), void *arg, uint64_t flags, void *stack);

struct proc {
    // 新增字段
    struct proc  *thread_group;  // 线程组头（NULL 表示主线程）
    int           tid;           // 线程 ID
    struct spinlock thread_lock; // 线程组锁
};
```

#### 与进程的区别

| 特性 | 进程（fork） | 线程（clone CLONE_VM） |
|------|-------------|----------------------|
| 地址空间 | 复制 | 共享（同一 pgdir） |
| 文件描述符 | 复制 | 可共享 |
| 内核栈 | 独立 | 独立 |
| 用户栈 | 复制 | 调用者提供 |
| 调度 | 独立时间片 | 独立时间片 |

---

### 阶段六：ELF 加载器 + 用户态程序

**目标**：实现 ELF64 程序加载，创建 initcode 和最简 shell。

#### 新增/修改文件

| 文件 | 类型 | 说明 |
|------|------|------|
| `include/elf.h` | 新增 | ELF64 格式定义（Elf64_Ehdr, Elf64_Phdr） |
| `kernel/exec.c` | 新增 | ELF64 加载执行（exec 系统调用实现） |
| `user/initcode.S` | 新增 | 第一个进程：exec("/init") |
| `user/init.c` | 新增 | init 进程：打开 console，fork+exec shell |
| `user/sh.c` | 新增 | 最简 shell（单行命令、管道、重定向） |
| `user/ulib.c` | 新增 | 用户库（系统调用包装函数） |
| `user/usys.S` | 新增 | 用户态系统调用入口汇编（SYSCALL 指令包装） |
| `user/user.h` | 新增 | 用户程序头文件（系统调用声明 + 类型） |

#### ELF64 加载流程

```c
// kernel/exec.c
int exec(char *path, char **argv) {
    // 1. namei(path) 找到 inode
    // 2. readi() 读取 ELF 头，验证 magic
    // 3. 分配新页表 uvmcreate()
    // 4. 遍历 PT_LOAD 段，mappages() 逐段映射
    // 5. 在用户地址空间顶部建立用户栈（2页：guard page + 栈）
    // 6. 将 argv[] 压入用户栈
    // 7. 替换 proc->pgdir，释放旧页表
    // 8. 更新 proc->tf->rip = ELF entry，tf->rsp = 用户栈顶
}
```

---

## 关键文件依赖关系

```
新增文件依赖树：
include/mmu.h          ← 被 vm.c, proc.h, 等引用
include/proc.h         ← 被 proc.c, syscall.c, trap.c 引用
include/sleeplock.h    ← 被 bio.c, fs.c 引用
include/buf.h          ← 被 bio.c, ide.c, log.c 引用
include/fs.h           ← 被 fs.c, log.c, mkfs.c 引用
include/file.h         ← 被 file.c, sysfile.c 引用
include/elf.h          ← 被 exec.c 引用

kernel/vm.c            ← 依赖 kalloc, mmu.h, spinlock
kernel/proc.c          ← 依赖 vm.c, swtch.S, spinlock, sleeplock
kernel/tss.c           ← 依赖 proc.h, x86_64.h
kernel/syscall_entry.S ← 依赖 proc.h, tss
kernel/syscall.c       ← 依赖 proc.c
kernel/sysproc.c       ← 依赖 proc.c, vm.c
kernel/ide.c           ← 依赖 x86_64.h, spinlock, buf.h
kernel/bio.c           ← 依赖 ide.c, sleeplock, buf.h
kernel/log.c           ← 依赖 bio.c, fs.h
kernel/fs.c            ← 依赖 log.c, bio.c, sleeplock
kernel/file.c          ← 依赖 fs.c, proc.h
kernel/pipe.c          ← 依赖 file.h, proc.c (sleep/wakeup)
kernel/exec.c          ← 依赖 fs.c, vm.c, proc.h, elf.h
kernel/sysfile.c       ← 依赖 file.c, fs.c, exec.c, proc.h
```

---

## Makefile 修改要点

```makefile
# 新增内核源文件
KERNEL_C_SRCS += \
    $(KERNEL)/vm.c       \
    $(KERNEL)/proc.c     \
    $(KERNEL)/tss.c      \
    $(KERNEL)/sleeplock.c \
    $(KERNEL)/syscall.c  \
    $(KERNEL)/sysproc.c  \
    $(KERNEL)/sysfile.c  \
    $(KERNEL)/ide.c      \
    $(KERNEL)/bio.c      \
    $(KERNEL)/log.c      \
    $(KERNEL)/fs.c       \
    $(KERNEL)/file.c     \
    $(KERNEL)/pipe.c     \
    $(KERNEL)/exec.c

# 新增汇编文件
KERNEL_S_SRCS += \
    $(KERNEL)/swtch.S         \
    $(KERNEL)/syscall_entry.S

# 新增用户态目标（ELF64 二进制）
USER_PROGS := init sh

# 文件系统镜像
fs.img: mkfs $(USER_PROGS)
    ./mkfs fs.img $(USER_PROGS)

# QEMU 参数：添加第二个磁盘（文件系统）
QEMU_ARGS += -drive file=build/fs.img,format=raw,index=1,media=disk
```

---

## 现有代码状态（重要）

批次 1 开始前，需了解以下已有定义，避免重复声明：

### `include/param.h`（已有）
- `NPROC=64`, `KSTACKSIZE=4096`, `NCPU=32`, `NOFILE=16`, `NFILE=100`, `MAXARG=32`
- `PGSIZE=4096`, `PGSHIFT=12`，**注意：已有 PGSIZE，mmu.h 中不再重复定义**

### `include/proc.h`（已有）
- `struct context`（r15/r14/r13/r12/rbx/rbp/rip）— 已正确定义
- `struct proc`（已有基础字段，`pgdir` 声明为 `pte_t *`，批次 1 后改为 `pml4e_t *`）
- `enum procstate`（已有）

### `include/defs.h`（已有）
- kalloc/spinlock/cpu/idt/panic/video/smp/acpi 已声明
- **批次 1 只需追加** vm.c 和 tss.c 的函数声明

### `kernel/cpu.c`（已有）
- `mycpu()` 目前返回固定的 `cpus[0]`，批次 2 会配合 GS 段寄存器改进

---

## 分批次实现顺序

### 批次 1（基础）：虚拟内存 + TSS + 内核页表切换
- `include/mmu.h`（新）—— **不重复定义已在 param.h 中的 PGSIZE**
- `include/memlayout.h`（改：加 KERNBASE、V2P/P2V、用户地址空间布局）
- `include/tss.h`（新）—— TSS 结构体
- `kernel/vm.c`（新）—— 页表管理
- `kernel/tss.c`（新）—— TSS 初始化，GDT 更新
- `include/defs.h`（改：追加 vm.c/tss.c 函数声明）
- `include/proc.h`（改：`pgdir` 改为 `pml4e_t *`）
- `linker/kernel.ld`（改：链接到 KERNBASE+0x100000 高地址）
- `kernel/entry64.S`（改：建立临时 PML4，`jmp` 跳转到高地址后继续）
- `kernel/main.c`（改：在 video_init 之前调用 kvminit + kvmswitch）

### 批次 2：进程管理
- `include/proc.h`（改/完善）
- `include/sleeplock.h`（新）
- `kernel/sleeplock.c`（新）
- `kernel/proc.c`（新）
- `kernel/swtch.S`（新）
- `include/param.h`（改：加 KSTACKSIZE/NOFILE/NFILE 等）

### 批次 3：系统调用
- `include/syscall.h`（新）
- `kernel/syscall_entry.S`（新）
- `kernel/syscall.c`（新）
- `kernel/sysproc.c`（新）

### 批次 4：磁盘驱动 + 文件系统
- `include/buf.h`（新）
- `include/fs.h`（新）
- `include/stat.h`（新）
- `include/file.h`（新）
- `kernel/ide.c`（新）
- `kernel/bio.c`（新）
- `kernel/log.c`（新）
- `kernel/fs.c`（新）
- `kernel/file.c`（新）
- `kernel/pipe.c`（新）
- `tools/mkfs.c`（新）

### 批次 5：ELF 加载 + 系统调用文件接口
- `include/elf.h`（新）
- `kernel/exec.c`（新）
- `kernel/sysfile.c`（新）

### 批次 6：线程支持
- `include/proc.h`（改：加 thread_group/tid）
- `kernel/proc.c`（改：实现 clone）
- `kernel/sysproc.c`（改：sys_clone）

### 批次 7：用户态程序
- `user/usys.S`（新）
- `user/ulib.c`（新）
- `user/user.h`（新）
- `user/initcode.S`（新）
- `user/init.c`（新）
- `user/sh.c`（新）
- `Makefile`（改：用户程序编译 + fs.img）

---

## 代码规范

1. **所有新增文件须有详细中文注释**（参考已有代码风格）
2. **不使用 GCC 内建函数**（stdarg 已完成自定义实现）
3. **头文件防重复包含**（`#ifndef __XXX_H__` 守卫）
4. **锁的使用原则**：
   - 修改进程状态前必须持有 `ptable.lock`
   - 文件系统操作使用 `sleeplock`（允许睡眠等待）
   - 短临界区使用 `spinlock`（禁止睡眠）
5. **内存管理约定**：
   - 物理地址 = `V2P(va)` / 虚拟地址 = `P2V(pa)`
   - `kalloc()` 返回内核虚拟地址（映射到高地址区）
   - 页表中存放的是物理地址

---

## 验证方案

### 批次 1 验证（虚拟内存）
- `make` 编译通过
- QEMU 启动，内核在高地址 KERNBASE+0x100000 运行
- kprintf 正常输出（证明高地址映射正确）

### 批次 2 验证（进程）
- QEMU 启动后有一个内核测试进程在运行（打印循环计数）
- 调度器正常工作（时钟中断触发切换）

### 批次 3 验证（系统调用）
- 测试代码用 SYSCALL 调用 SYS_getpid，返回正确 PID
- SYS_write 能输出到 console

### 批次 4 验证（文件系统）
- mkfs 生成 fs.img
- QEMU 挂载两个磁盘正常启动
- 能 ls /，读取文件内容

### 批次 5 验证（ELF + sysfile）
- exec("/init") 成功加载并运行用户程序
- init 能 fork + exec

### 批次 6 验证（线程）
- clone() 创建线程，共享地址空间，独立运行

### 批次 7 验证（shell）
- shell 启动，能执行 `ls`、`echo hello`、`cat file` 命令
- 管道 `ls | wc` 正常工作
