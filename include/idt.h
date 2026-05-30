#ifndef __IDT_H__
#define __IDT_H__

#include <types.h>

/*---------------------------------------------------------------------------
 * 64位 IDT 门描述符 (16字节)
 *---------------------------------------------------------------------------*/
struct idt_entry {
    uint16_t offset_0_15;   /* 偏移量 bit[0:15] */
    uint16_t selector;      /* 段选择子 */
    uint8_t  ist;           /* IST 索引 (0 = 不使用 IST) */
    uint8_t  type_attr;     /* 类型与属性 */
    uint16_t offset_16_31;  /* 偏移量 bit[16:31] */
    uint32_t offset_32_63;  /* 偏移量 bit[32:63] */
    uint32_t reserved;      /* 保留 (必须为 0) */
} __attribute__((packed));

/*---------------------------------------------------------------------------
 * 64位陷阱帧 — 由硬件和汇编保存
 *---------------------------------------------------------------------------*/
struct trapframe {
    /* 通用寄存器 (由 trap_entry.S 手动保存) */
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx;
    uint64_t rdx, rcx, rax;

    /* 中断号 (由汇编推入) */
    uint64_t trapno;

    /* 错误码 (由硬件或汇编推入; 若无错误码则推入 0) */
    uint64_t err;

    /* 由硬件保存 */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/*---------------------------------------------------------------------------
 * IDT 门描述符属性
 *---------------------------------------------------------------------------*/
/* 门类型 */
#define IDT_PRESENT    0x80     /* Present 位 */
#define IDT_INTERRUPT  0x0E     /* 中断门 (禁止中断) */
#define IDT_TRAP       0x0F     /* 陷阱门 (不禁止中断) */
#define IDT_DPL0       0x00     /* DPL = 0 (内核级) */
#define IDT_DPL3       0x60     /* DPL = 3 (用户级) */

/* 段选择子 (与 tss.c 重建的 GDT 一致，参考 mmu.h)
 * 64位长模式中断处理必须使用64位代码段 (L=1)
 * Index 0: NULL      (0x00)
 * Index 1: CODE32    (0x08) — 32位保护模式代码段（保留兼容）
 * Index 2: DATA32    (0x10) — 32位保护模式数据段（保留兼容）
 * Index 3: CODE64    (0x18) — 64位长模式代码段 (L=1, D=0)，CPL=0
 * Index 4: DATA64    (0x20) — 64位数据段，CPL=0
 * Index 5: CODE64    (0x28) — 64位长模式代码段，CPL=3（用户态）
 * Index 6: DATA64    (0x30) — 64位数据段，CPL=3（用户态）
 * Index 7+8: TSS     (0x38) — 64位TSS描述符（16字节，占两个slot）
 *
 * 段选择子常量定义在 mmu.h 中，此处不重复定义。
 */
#include <mmu.h>

/*---------------------------------------------------------------------------
 * 中断/异常号定义 (参考 xv6 traps.h)
 *---------------------------------------------------------------------------*/
/* CPU 异常 */
#define T_DIVIDE     0     /* 除法错误 */
#define T_DEBUG      1     /* 调试异常 */
#define T_NMI        2     /* 非可屏蔽中断 */
#define T_BRKPT      3     /* 断点 */
#define T_OVERFLOW   4     /* 溢出 */
#define T_BOUND      5     /* BOUND 范围超出 */
#define T_ILLOP      6     /* 非法操作码 */
#define T_DEVICE     7     /* 设备不可用 */
#define T_DFLT       8     /* 双重故障 (有错误码) */
#define T_COPROC     9     /* 协处理器段超出 */
#define T_TSS       10     /* 无效 TSS (有错误码) */
#define T_SEGNP     11     /* 段不存在 (有错误码) */
#define T_STACK     12     /* 栈段故障 (有错误码) */
#define T_GPFLT     13     /* 一般保护故障 (有错误码) */
#define T_PGFLT     14     /* 页故障 (有错误码) */
#define T_SPURIOUS  15     /* 假中断 */
#define T_FPERR     16     /* 浮点错误 */
#define T_ALIGN     17     /* 对齐检查 (有错误码) */
#define T_MCHK      18     /* 机器检查 */
#define T_SIMDERR   19     /* SIMD 浮点错误 */

/* IRQ 中断 (从 32 开始) */
#define T_IRQ0      32
#define IRQ_TIMER    0     /* 时钟中断 */
#define IRQ_KBD      1     /* 键盘 */
#define IRQ_CASCADE  2     /* PIC 级联 */
#define IRQ_UART1    3     /* COM2/COM4 */
#define IRQ_UART2    4     /* COM1/COM3 */
#define IRQ_MOUSE   12     /* PS/2 鼠标 */
#define IRQ_IDE     14     /* IDE 磁盘 */
#define IRQ_IDE2    15     /* 第二 IDE */
#define IRQ_ERROR   19     /* LAPIC 错误 */
#define IRQ_SPURIOUS 31    /* LAPIC 假中断 */

/* 系统调用 (未来) */
#define T_SYSCALL   0x80

/* IDT 最大条目数 */
#define IDT_ENTRIES  256

/*---------------------------------------------------------------------------
 * 接口函数
 *---------------------------------------------------------------------------*/
void idt_init(void);
void idt_set_gate(int num, void *handler, uint8_t type_attr, uint16_t selector);
void trap_handler(struct trapframe *tf);

#endif /* __IDT_H__ */