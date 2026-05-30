# WinixOS 内核 libc 库与基础设施实现计划

## Context

WinixOS 当前仅有极简的基础函数（`kernel/util.c` 中 7 个 k 前缀函数），缺少内核 libc 库和关键基础设施（中断、内存分配、同步、进程结构）。参考 xv6-public 的架构和代码风格，为 64 位 WinixOS 建立完整的内核 libc 库和基础设施，使系统具备真正的操作系统骨架，为后续开发（进程调度、文件系统、系统调用）奠定基础。

**用户决策**：去掉 k 前缀使用标准命名、独立 lib 目录、全面基础设施范围、64位优化指令。

---

## 目标目录结构

```
winixos/
├── kernel/
│   ├── entry64.S              # (保留) BSP 入口
│   ├── ap_trampoline.S        # (保留) AP 蹦床
│   ├── main.c                 # (修改) 适配新初始化流程
│   ├── video.c/h              # (保留) 视频输出
│   ├── acpi.c/h               # (保留) ACPI 解析
│   ├── smp.c/h                # (保留) SMP 多核
│   ├── font.c                 # (保留) PSF2 字体
│   ├── lib/                   # ★ 新建：内核 libc 库
│   │   ├── string.c           # string/memory 操作 (64位优化)
│   │   ├── stdio.c            # kprintf 格式化输出
│   │   ├── ctype.c            # 字符分类函数
│   │   └── assert.c           # 断言与 panic
│   ├── kalloc.c/h             # ★ 新建：物理内存分配器
│   ├── spinlock.c/h           # ★ 新建：自旋锁
│   ├── idt.c/h                # ★ 新建：IDT 与中断处理框架
│   ├── proc.h                 # ★ 新建：进程结构体定义
│   └── cpu.h                  # ★ 新建：CPU 结构体定义
│   └── panic.c                # ★ 新建：内核 panic 处理
├── include/
│   ├── types.h                # (修改) 扩展类型定义
│   ├── param.h                # ★ 新建：系统参数配置
│   ├── memlayout.h            # ★ 新建：内存布局常量
│   ├── x86_64.h               # ★ 新建：x86-64 内联汇编接口
│   ├── boot_info.h            # (保留)
│   ├── e820.h                 # (保留)
│   ├── font.h                 # (保留)
│   ├── libc.h                 # ★ 新建：libc 函数统一头文件
│   ├── stdio.h                # ★ 新建：printf 相关声明
│   ├── string.h               # ★ 新建：string 函数声明
│   ├── ctype.h                # ★ 新建：字符分类声明
│   └── assert.h               # ★ 新建：assert/panic 宏
│   ├── spinlock.h             # ★ 新建：自旋锁声明
│   ├── kalloc.h               # ★ 新建：kalloc 声明
│   ├── idt.h                  # ★ 新建：IDT 声明
│   ├── proc.h                 # ★ 新建：进程结构体声明
│   └── cpu.h                  # ★ 新建：CPU 结构体声明
│   └── defs.h                 # ★ 新建：全局函数声明汇总
├── linker/                    # (保留)
├── boot/                      # (保留)
├── Makefile                   # (修改) 添加新文件编译
```

---

## 实现步骤（按优先级排序）

### Step 1: 扩展类型系统 — `include/types.h`

**修改文件**: `include/types.h`

扩展当前 types.h，添加更多实用类型：

```c
// 新增类型
typedef uint8_t   bool;
#define true  1
#define false 0

typedef int64_t   ssize_t;
typedef int64_t   off_t;

// 页表条目类型 (64位)
typedef uint64_t  pte_t;
typedef uint64_t  pde_t;   // PML4/PDPT/PD 条目

// min/max 宏
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// 对齐宏
#define ALIGN_UP(x, a)   (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)
```

---

### Step 2: 系统参数 — `include/param.h`

**新建文件**: `include/param.h`

参考 xv6 的 param.h，适配 64 位和 WinixOS 需求：

```c
#define NPROC         64     // 最大进程数
#define KSTACKSIZE    4096   // 每进程内核栈大小
#define NCPU          32     // 最大 CPU 数 (与当前 SMP 一致)
#define NOFILE        16     // 每进程打开文件数
#define NFILE         100    // 系统全局打开文件数
#define PGSIZE        4096   // 页大小
#define PGSHIFT       12     // 页偏移位数
#define MAXARG        32     // exec 最大参数数
```

---

### Step 3: 内存布局 — `include/memlayout.h`

**新建文件**: `include/memlayout.h`

定义 WinixOS 64位内存布局常量（当前使用 1:1 物理映射）：

```c
// 物理地址布局 (与 bootloader 协商)
#define KERN_PHYS_BASE   0x100000    // 内核物理起始
#define PHYSTOP          0x80000000  // 物理内存上限 (2GB, 可根据 E820 动态)

// 内核符号 (由 linker/kernel.ld 导出)
extern char __kernel_end[];  // 内核镜像结束地址

// 页对齐宏
#define PGROUNDUP(x)   ALIGN_UP(x, PGSIZE)
#define PGROUNDDOWN(x) ALIGN_DOWN(x, PGSIZE)
```

---

### Step 4: x86-64 内联汇编接口 — `include/x86_64.h`

**新建文件**: `include/x86_64.h`

参考 xv6 的 x86.h，全面适配 64 位。这是内核与硬件交互的基础：

**I/O 端口操作**:
```c
static inline uint8_t  inb(uint16_t port);
static inline uint16_t inw(uint16_t port);
static inline uint32_t inl(uint16_t port);
static inline void outb(uint16_t port, uint8_t data);
static inline void outw(uint16_t port, uint16_t data);
static inline void outl(uint16_t port, uint32_t data);
static inline void insl(uint16_t port, void *addr, int cnt);
static inline void outsl(uint16_t port, const void *addr, int cnt);
```

**内存块操作 (64位优化)**:
```c
// stosb: 按字节填充 (用于非对齐情况)
static inline void stosb(void *addr, int data, size_t cnt);

// stosq: 按8字节填充 (64位优化，对齐时使用)
// 替代 xv6 的 stosl，8字节一次操作性能翻倍
static inline void stosq(void *addr, uint64_t data, size_t cnt);

// movsq: 按8字节复制 (64位优化)
static inline void movsq(void *dst, const void *src, size_t cnt);
```

**控制寄存器操作**:
```c
static inline uint64_t rcr0(void);
static inline uint64_t rcr2(void);
static inline uint64_t rcr3(void);
static inline uint64_t rcr4(void);
static inline void lcr3(uint64_t val);
```

**中断控制**:
```c
static inline void cli(void);
static inline void sti(void);
static inline uint64_t read_rflags(void);
```

**原子操作 (64位)**:
```c
// xchg: 原子交换 (64位版本，使用 lock; xchgq)
static inline uint64_t xchg64(volatile uint64_t *addr, uint64_t newval);

// 32位版本也保留 (用于 spinlock.locked)
static inline uint32_t xchg32(volatile uint32_t *addr, uint32_t newval);
```

**GDT/IDT 加载 (64位)**:
```c
// lgdt/lidt 64位版本，使用10字节伪描述符 (2字节limit + 8字节base)
struct desc_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static inline void lgdt64(struct desc_ptr *p);
static inline void lidt64(struct desc_ptr *p);
```

---

### Step 5: 内核 libc — `kernel/lib/string.c`

**新建文件**: `kernel/lib/string.c`, 对应头文件 `include/string.h`

去掉 k 前缀，参考 xv6 的 string.c，全面适配 64 位并使用优化指令：

**内存操作**:
```c
// memset — 64位优化版
// 对齐到8字节时使用 stosq (一次填充8字节)
// 否则按字节 stosb
void *memset(void *dst, int c, size_t n);

// memcpy — 使用 movsq 优化
// 对齐时按8字节复制，否则逐字节
void *memcpy(void *dst, const void *src, size_t n);

// memmove — 处理重叠情况
// 参考 xv6 实现，但使用64位优化
void *memmove(void *dst, const void *src, size_t n);

// memcmp — 逐字节比较
int memcmp(const void *v1, const void *v2, size_t n);
```

**字符串操作**:
```c
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *safestrcpy(char *dst, const char *src, size_t n);  // xv6 风格安全复制
char  *strcat(char *dst, const char *src);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
```

**格式化转换** (从 util.c 迁移):
```c
char *uitoa(uint64_t n, char *buf, int base);
char *itoa(int64_t n, char *buf);
char *u64_to_hex(uint64_t n, char *buf);
```

**关键: memset 64位优化实现示例**:
```c
void *memset(void *dst, int c, size_t n) {
    if (((uintptr_t)dst & 7) == 0 && (n & 7) == 0) {
        // 8字节对齐，使用 stosq
        uint64_t pattern = (uint8_t)c;
        pattern |= pattern << 8;
        pattern |= pattern << 16;
        pattern |= pattern << 32;  // 64位扩展
        stosq(dst, pattern, n / 8);
    } else if (((uintptr_t)dst & 3) == 0 && (n & 3) == 0) {
        // 4字节对齐，使用 stosl (兼容)
        uint32_t pattern = (uint8_t)c;
        pattern |= pattern << 8;
        pattern |= pattern << 16;
        // 用 stosq 替代，n/4 个 4字节 = n/8 后剩余...
        // 简化：用 stosb 处理不对齐情况
        stosb(dst, c, n);
    } else {
        stosb(dst, c, n);
    }
    return dst;
}
```

---

### Step 6: 字符分类 — `kernel/lib/ctype.c`

**新建文件**: `kernel/lib/ctype.c`, 对应 `include/ctype.h`

```c
bool isdigit(int c);    // '0'-'9'
bool isalpha(int c);    // 'a'-'z', 'A'-'Z'
bool isalnum(int c);    // isalpha || isdigit
bool isupper(int c);    // 'A'-'Z'
bool islower(int c);    // 'a'-'z'
bool isspace(int c);    // ' ', '\t', '\n', '\r', '\f', '\v'
bool isprint(int c);    // 可打印字符
int  toupper(int c);
int  tolower(int c);
```

---

### Step 7: 格式化输出 — `kernel/lib/stdio.c`

**新建文件**: `kernel/lib/stdio.c`, 对应 `include/stdio.h`

实现内核 kprintf（名称保留 k 以区分用户态 printf），参考 xv6 的 console.c/cprintf：

```c
// kprintf — 内核格式化输出到屏幕
// 支持: %d, %u, %x, %p, %s, %c, %l (64位修饰符)
// 例: kprintf("CPU %d online, memory %lu MB\n", id, mb);
void kprintf(const char *fmt, ...);

// kprintf_color — 带颜色的格式化输出
void kprintf_color(uint32_t fg, const char *fmt, ...);

// sprintf — 格式化到字符串缓冲区
int sprintf(char *buf, const char *fmt, ...);

// snprintf — 安全版本，限制长度
int snprintf(char *buf, size_t size, const char *fmt, ...);
```

**kprintf 实现要点**:
- 使用变参 (stdarg.h，但内核需要自己定义 va_list)
- 内核内部定义 `include/stdarg.h` (va_start/va_arg/va_end/va_copy)
- 底层调用 video_print 输出
- 支持 %d/%u/%x/%p/%s/%c/%lld 等格式

**内核 stdarg.h**:
```c
// include/stdarg.h — 内核变参支持
typedef __builtin_va_list va_list;
#define va_start(ap, last)   __builtin_va_start(ap, last)
#define va_arg(ap, type)     __builtin_va_arg(ap, type)
#define va_end(ap)           __builtin_va_end(ap)
#define va_copy(dest, src)   __builtin_va_copy(dest, src)
```

---

### Step 8: 断言与 panic — `kernel/lib/assert.c` + `kernel/panic.c`

**新建文件**: `kernel/lib/assert.c`, `kernel/panic.c`, 对应 `include/assert.h`

```c
// include/assert.h
#define assert(cond) \
    do { if (!(cond)) panic("assertion failed: " #cond); } while(0)

// kernel/panic.c
void panic(const char *msg) {
    kprintf_color(COLOR_RED, "PANIC: %s\n", msg);
    // 打印调用栈信息 (如果可能)
    // 永久停机
    cli();
    while (1) hlt();
}
```

---

### Step 9: CPU 结构体 — `kernel/cpu.h`

**新建文件**: `include/cpu.h`, `kernel/cpu.h`

参考 xv6 的 struct cpu，适配64位和 WinixOS:

```c
struct cpu {
    uint8_t  apicid;           // LAPIC ID
    uint8_t  started;          // 是否已启动
    int      ncli;             // pushcli 嵌套深度
    int      intena;           // pushcli 前中断是否开启
    struct proc *proc;         // 当前运行的进程
};

// 每个 CPU 的栈地址 (BSP: 0x580000, AP: 动态分配)
extern struct cpu cpus[NCPU];

// 获取当前 CPU 编号
int cpuid(void);
struct cpu *mycpu(void);
```

---

### Step 10: 自旋锁 — `kernel/spinlock.c/h`

**新建文件**: `kernel/spinlock.c`, `kernel/spinlock.h`, `include/spinlock.h`

参考 xv6 的 spinlock 实现，适配64位：

```c
// include/spinlock.h
struct spinlock {
    uint32_t locked;      // 是否被持有 (用32位原子操作)
    char    *name;        // 锁名称 (调试)
    struct cpu *cpu;      // 持有锁的 CPU
};

void initlock(struct spinlock *lk, char *name);
void acquire(struct spinlock *lk);
void release(struct spinlock *lk);
int  holding(struct spinlock *lk);
void pushcli(void);
void popcli(void);
```

**64位适配要点**:
- xchg 使用 `xchg32` (锁字段用32位足够)
- pushcli/popcli 使用 `read_rflags()` 检查 IF 位
- `FL_IF` 定义为 `0x200` (RFLAGS 第9位)

---

### Step 11: 物理内存分配器 — `kernel/kalloc.c/h`

**新建文件**: `kernel/kalloc.c`, `kernel/kalloc.h`, `include/kalloc.h`

参考 xv6 的 kalloc.c，适配64位。当前 WinixOS 已解析 E820 内存映射：

```c
// include/kalloc.h
void kinit(void);           // 初始化分配器 (从 E820 可用区域)
void *kalloc(void);         // 分配一个 4096 字节物理页
void kfree(void *v);        // 释放一个物理页

// kernel/kalloc.c
struct run {
    struct run *next;
};

struct {
    struct spinlock lock;
    struct run *freelist;
} kmem;

// kinit: 遍历 E820 usable 区域，将可用页加入 freelist
// kalloc: 从 freelist 取出一页
// kfree: 填充垃圾值，放回 freelist
```

**64位适配要点**:
- `struct run` 的指针是64位
- `__kernel_end` 来自链接脚本
- 不需要 V2P/P2V 转换（当前1:1映射）
- 可用内存范围: `__kernel_end` 到 PHYSTOP (基于 E820 动态计算)

---

### Step 12: IDT 与中断处理框架 — `kernel/idt.c/h`

**新建文件**: `kernel/idt.c`, `kernel/idt.h`, `include/idt.h`

这是64位关键差异点。xv6 使用32位中断门描述符，WinixOS 需要64位版本：

```c
// include/idt.h — 64位 IDT 门描述符
struct idt_entry {
    uint16_t offset_0_15;   // 偏移 [0:15]
    uint16_t selector;      // 段选择子
    uint8_t  ist;           // IST 索引 (0=不用)
    uint8_t  type_attr;     // 类型与属性
    uint16_t offset_16_31;  // 偏移 [16:31]
    uint32_t offset_32_63;  // 偏移 [32:63]
    uint32_t reserved;      // 保留
} __attribute__((packed));

// 64位陷阱帧 (由硬件和汇编保存)
struct trapframe {
    // 通用寄存器 (由汇编保存)
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx;
    uint64_t rdx, rcx, rax;
    // 中断号和错误码
    uint64_t trapno;
    uint64_t err;
    // 硬件保存部分
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

void idt_init(void);          // 初始化 IDT
void idt_set_gate(int num, void *handler, uint8_t type_attr, uint16_t selector);

// 中断号定义 (参考 xv6 traps.h)
#define T_DIVIDE     0
#define T_DEBUG      1
#define T_NMI        2
#define T_BRKPT      3
#define T_OVERFLOW   4
#define T_BOUND      5
#define T_ILLOP      6
#define T_DEVICE     7
#define T_DFLT       8
#define T_COPROC     9
#define T_TSS       10
#define T_SEGNP     11
#define T_STACK     12
#define T_GPFLT     13
#define T_PGFLT     14
// ... 更多异常号
#define T_SYSCALL   0x80      // 系统调用 (未来)
```

**还需要一个汇编文件**: `kernel/trap_entry.S`
- 保存所有通用寄存器到 trapframe
- 调用 C 函数 `trap_handler(struct trapframe *tf)`
- 从 trapframe 恢复寄存器并 iretq

---

### Step 13: 基本进程结构定义 — `kernel/proc.h`

**新建文件**: `include/proc.h`, `kernel/proc.h`

参考 xv6 的 proc.h，定义64位进程结构体（暂不实现调度逻辑）：

```c
// 进程状态
enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// 上下文保存 (用于内核上下文切换，参考 xv6 context)
struct context {
    uint64_t r15, r14, r13, r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rip;
};

// 进程结构体
struct proc {
    uint64_t        sz;          // 进程内存大小
    pte_t          *pgdir;       // 页表指针 (PML4)
    char           *kstack;      // 内核栈底
    enum procstate  state;       // 进程状态
    int             pid;         // 进程 ID
    struct proc    *parent;      // 父进程
    struct trapframe *tf;        // 陷阱帧
    struct context   *context;   // 切换上下文
    void           *chan;        // 等待通道
    int             killed;      // 是否被杀
    char            name[16];    // 进程名
};

// 进程表
extern struct proc ptable[NPROC];
extern struct spinlock ptable_lock;
```

---

### Step 14: 全局函数声明 — `include/defs.h`

**新建文件**: `include/defs.h`

参考 xv6 的 defs.h，汇总所有模块的函数声明：

```c
// kalloc.c
void kinit(void);
void *kalloc(void);
void kfree(void *);

// spinlock.c
void initlock(struct spinlock *, char *);
void acquire(struct spinlock *);
void release(struct spinlock *);
int holding(struct spinlock *);

// idt.c
void idt_init(void);
void trap_handler(struct trapframe *);

// panic.c
void panic(const char *);

// proc.c (未来)
// ...

// video.c
void video_init(uint64_t, uint32_t, uint32_t, uint32_t, uint8_t);
void video_print(const char *, uint32_t);

// smp.c
int smp_init(BootInfo *);
int smp_cpu_count(void);

// acpi.c
void acpi_init(uint64_t);
ACPI_Header *acpi_find_table(const char *);
```

---

### Step 15: libc 统一头文件 — `include/libc.h`

**新建文件**: `include/libc.h`

```c
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
```

---

### Step 16: 修改现有文件

**删除 `kernel/util.c` 和 `kernel/util.h`**:
- 函数已迁移到 `kernel/lib/string.c`
- 所有使用 util.h 的文件改为使用 `include/string.h` 等

**修改 `kernel/main.c`**:
- 删除 `#include "util.h"`，改为 `#include <libc.h>`
- 删除 `#include <types.h>` (libc.h 内含)
- 在 kernel_main 中增加初始化调用:
  ```c
  kinit();      // 物理内存分配器初始化
  idt_init();   // IDT 初始化
  ```
- 将手动拼接字符串的代码改为 kprintf:
  ```c
  // 旧代码：
  char wbuf[8]; uitoa(info->fb_width, wbuf, 10);
  // 新代码：
  kprintf_color(COLOR_WHITE, "Display  : %ux%ux32\n", info->fb_width, info->fb_height);
  ```

**修改 `kernel/smp.c`**:
- `#include "util.h"` → `#include <string.h>` (用 memcpy 替代 kmemcpy)

**修改 `kernel/video.c`**:
- 可选：添加 kprintf_color 的底层回调接口

**修改 `Makefile`**:
- 添加 KERNEL_C_SRCS 中的新文件
- 添加 `-I include` (已有)
- 添加 lib/ 子目录编译规则
- 移除 util.c
- 新增汇编文件 kernel/trap_entry.S

```makefile
KERNEL_C_SRCS := \
    $(KERNEL)/main.c  \
    $(KERNEL)/video.c \
    $(KERNEL)/acpi.c  \
    $(KERNEL)/smp.c   \
    $(KERNEL)/font.c  \
    $(KERNEL)/kalloc.c   \
    $(KERNEL)/spinlock.c \
    $(KERNEL)/idt.c      \
    $(KERNEL)/panic.c    \
    $(KERNEL)/lib/string.c \
    $(KERNEL)/lib/ctype.c  \
    $(KERNEL)/lib/stdio.c  \
    $(KERNEL)/lib/assert.c

KERNEL_S_OBJS := \
    $(BUILD)/$(KERNEL)/entry64.o \
    $(BUILD)/$(KERNEL)/trap_entry.o
```

---

## 64位关键适配总结

| 特性 | xv6 (32位) | WinixOS (64位) |
|------|-----------|---------------|
| 地址宽度 | uint (32位) | uint64_t / uintptr_t (64位) |
| 页表级数 | 2级 (PD+PT) | 4级 (PML4+PDPT+PD+PT) |
| 页表条目 | uint pde_t | uint64_t pte_t/pde_t |
| IDT门描述符 | 8字节 (32位) | 16字节 (64位，含 offset_32_63) |
| Trapframe | pusha 保存 | 手动保存全部16个通用寄存器 |
| Context切换 | push 4个寄存器 | push 7个寄存器 (r12-r15+rbx+rbp+rip) |
| 原子操作 | xchgl (32位) | xchgq (64位) 或 xchgl (32位锁字段) |
| 内存优化 | rep stosl (4字节) | rep stosq/movsq (8字节) |
| lgdt/lidt | 6字节伪描述符 | 10字节伪描述符 (2+8) |
| 内核栈 | 4096字节 | 4096字节 (不变) |
| I/O端口 | inb/outb 16位端口 | 相同 (I/O端口空间不变) |

---

## 实现顺序建议

按依赖关系排序，逐步构建：

1. **include/types.h** (扩展) — 基础类型，所有文件依赖
2. **include/param.h** — 系统常量
3. **include/memlayout.h** — 内存布局
4. **include/x86_64.h** — 硬件接口 (string.c 需要 stosb/stosq)
5. **include/stdarg.h** — 变参支持 (stdio.c 需要)
6. **include/string.h + kernel/lib/string.c** — libc 核心，替代 util.c
7. **include/ctype.h + kernel/lib/ctype.c** — 字符分类
8. **include/assert.h + kernel/lib/assert.c + kernel/panic.c** — 断言和 panic
9. **include/spinlock.h + kernel/spinlock.c** — 自旋锁 (kalloc 需要)
10. **include/kalloc.h + kernel/kalloc.c** — 内存分配器 (proc 需要)
11. **include/stdio.h + kernel/lib/stdio.c** — kprintf 格式化输出
12. **include/cpu.h + kernel/cpu.h** — CPU 结构体
13. **include/idt.h + kernel/idt.h/c + kernel/trap_entry.S** — 中断框架
14. **include/proc.h + kernel/proc.h** — 进程结构体定义
15. **include/defs.h** — 全局声明汇总
16. **include/libc.h** — libc 统一头文件
17. **修改 kernel/main.c** — 集成新模块
18. **修改 Makefile** — 编译系统更新
19. **删除 kernel/util.c/h** — 旧代码清理

---

## 验证方法

1. **编译验证**: `make clean && make all` — 确保所有新文件编译通过，无编译错误
2. **运行验证**: `make run` — QEMU 启动，检查：
   - 原有的视频输出和系统信息显示正常
   - kprintf 输出格式化字符串正确
   - SMP 多核初始化正常
   - IDT 加载后系统不崩溃
   - kalloc 分配/释放正常 (通过 kprintf 打印分配信息验证)
3. **逐步验证策略**: 每完成一个模块就编译运行测试，而不是全部完成后才测试

---

## 关键文件路径汇总

**新建文件**:
- `include/types.h` (修改)
- `include/param.h`
- `include/memlayout.h`
- `include/x86_64.h`
- `include/stdarg.h`
- `include/string.h`
- `include/ctype.h`
- `include/assert.h`
- `include/stdio.h`
- `include/spinlock.h`
- `include/kalloc.h`
- `include/cpu.h`
- `include/idt.h`
- `include/proc.h`
- `include/defs.h`
- `include/libc.h`
- `kernel/lib/string.c`
- `kernel/lib/ctype.c`
- `kernel/lib/stdio.c`
- `kernel/lib/assert.c`
- `kernel/spinlock.c`
- `kernel/spinlock.h`
- `kernel/kalloc.c`
- `kernel/kalloc.h`
- `kernel/idt.c`
- `kernel/idt.h`
- `kernel/panic.c`
- `kernel/cpu.h`
- `kernel/proc.h`
- `kernel/trap_entry.S`

**修改文件**:
- `kernel/main.c` — 适配新初始化流程和 kprintf
- `kernel/smp.c` — util.h → string.h
- `kernel/video.c` — 可能添加 kprintf 回调
- `Makefile` — 添加新文件、移除 util.c
- `linker/kernel.ld` — 可选：导出更多内核符号

**删除文件**:
- `kernel/util.c`
- `kernel/util.h`