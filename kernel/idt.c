#include <types.h>
#include <string.h>
#include <idt.h>
#include <x86_64.h>
#include <assert.h>
#include <stdio.h>
#include <lapic.h>
#include "video.h"

/*---------------------------------------------------------------------------
 * IDT 与中断处理框架 — 64位实现
 *
 * xv6 使用32位中断门，WinixOS 需要64位版本:
 * - IDT 门描述符为16字节 (含 offset_32_63 字段)
 * - lgdt/lidt 使用10字节伪描述符 (2字节limit + 8字节base)
 * - iretq (64位中断返回)
 *---------------------------------------------------------------------------*/

/* IDT 数组 (256 条目) */
struct idt_entry idt[IDT_ENTRIES];

/* 各异常/中断处理函数声明 (由 trap_entry.S 提供) */
extern void handler_divide(void);
extern void handler_debug(void);
extern void handler_nmi(void);
extern void handler_brkpt(void);
extern void handler_overflow(void);
extern void handler_bound(void);
extern void handler_illop(void);
extern void handler_device(void);
extern void handler_dflt(void);
extern void handler_coproc(void);
extern void handler_tss(void);
extern void handler_segnp(void);
extern void handler_stack(void);
extern void handler_gpflt(void);
extern void handler_pgflt(void);
extern void handler_spurious(void);
extern void handler_fperr(void);
extern void handler_align(void);
extern void handler_mchk(void);
extern void handler_simderr(void);

/* IRQ handler 地址表 (由 trap_entry.S 提供, 包含 224 个 handler 指针) */
extern uint64_t irq_handler_table[];

/*---------------------------------------------------------------------------
 * idt_set_gate — 设置 IDT 条目
 *---------------------------------------------------------------------------*/
void idt_set_gate(int num, void *handler, uint8_t type_attr, uint16_t selector)
{
    uint64_t addr = (uint64_t)handler;

    idt[num].offset_0_15  = (uint16_t)(addr & 0xFFFF);
    idt[num].selector     = selector;
    idt[num].ist          = 0;           /* 不使用 IST */
    idt[num].type_attr    = type_attr;
    idt[num].offset_16_31 = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[num].offset_32_63 = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[num].reserved     = 0;
}

/*---------------------------------------------------------------------------
 * idt_init — 初始化 IDT 并加载
 *---------------------------------------------------------------------------*/
void idt_init(void)
{
    /* 清零 IDT */
    memset(idt, 0, sizeof(idt));

    /* 设置 CPU 异常处理 (0-19) */
    idt_set_gate(T_DIVIDE,   handler_divide,    IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_DEBUG,    handler_debug,     IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_NMI,      handler_nmi,       IDT_PRESENT | IDT_INTERRUPT | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_BRKPT,    handler_brkpt,     IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_OVERFLOW, handler_overflow,  IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_BOUND,    handler_bound,     IDT_PRESENT | IDT_INTERRUPT | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_ILLOP,    handler_illop,     IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_DEVICE,   handler_device,    IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_DFLT,     handler_dflt,      IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_COPROC,   handler_coproc,    IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_TSS,      handler_tss,       IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_SEGNP,    handler_segnp,     IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_STACK,    handler_stack,      IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_GPFLT,    handler_gpflt,     IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_PGFLT,    handler_pgflt,     IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_SPURIOUS, handler_spurious,  IDT_PRESENT | IDT_INTERRUPT | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_FPERR,    handler_fperr,     IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_ALIGN,    handler_align,     IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_MCHK,     handler_mchk,      IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);
    idt_set_gate(T_SIMDERR,  handler_simderr,   IDT_PRESENT | IDT_TRAP | IDT_DPL0, KERN_CODE_SEL);

    /* 设置 IRQ 中断处理 (32-255)，使用独立 handler 地址表 */
    for (int i = T_IRQ0; i < IDT_ENTRIES; i++)
        idt_set_gate(i, (void *)irq_handler_table[i - T_IRQ0], IDT_PRESENT | IDT_INTERRUPT | IDT_DPL0, KERN_CODE_SEL);

    /* 加载 IDT */
    struct desc_ptr idt_desc;
    idt_desc.limit = sizeof(idt) - 1;
    idt_desc.base  = (uint64_t)&idt;
    lidt64(&idt_desc);
}

/*---------------------------------------------------------------------------
 * trap_handler — C 语言陷阱/中断处理入口
 * 由 trap_entry.S 调用
 *---------------------------------------------------------------------------*/
void trap_handler(struct trapframe *tf)
{
    /* CPU 异常处理 */
    if (tf->trapno <= T_SIMDERR) {
        kprintf_color(COLOR_RED, "\nException %lu: ", tf->trapno);
        switch (tf->trapno) {
        case T_DIVIDE:   kprintf("Division Error\n"); break;
        case T_DEBUG:    kprintf("Debug Exception\n"); break;
        case T_NMI:      kprintf("NMI\n"); break;
        case T_BRKPT:    kprintf("Breakpoint\n"); break;
        case T_OVERFLOW: kprintf("Overflow\n"); break;
        case T_BOUND:    kprintf("BOUND Range Exceeded\n"); break;
        case T_ILLOP:    kprintf("Invalid Opcode\n"); break;
        case T_DEVICE:   kprintf("Device Not Available\n"); break;
        case T_DFLT:     kprintf("Double Fault, err=%lu\n", tf->err); break;
        case T_TSS:      kprintf("Invalid TSS, err=%lu\n", tf->err); break;
        case T_SEGNP:    kprintf("Segment Not Present, err=%lu\n", tf->err); break;
        case T_STACK:    kprintf("Stack Fault, err=%lu\n", tf->err); break;
        case T_GPFLT:    kprintf("General Protection, err=%lu\n", tf->err); break;
        case T_PGFLT:    kprintf("Page Fault, err=%lu\n", tf->err); break;
        case T_FPERR:    kprintf("Floating Point Error\n"); break;
        case T_ALIGN:    kprintf("Alignment Check, err=%lu\n", tf->err); break;
        case T_MCHK:     kprintf("Machine Check\n"); break;
        case T_SIMDERR:  kprintf("SIMD Floating Point Error\n"); break;
        default:         kprintf("Unknown Exception\n"); break;
        }
        kprintf_color(COLOR_LGRAY, "  RIP=%p RSP=%p\n", tf->rip, tf->rsp);
        panic("unhandled exception");
    }

    /* IRQ 中断处理 (32+) */
    int irq = (int)(tf->trapno - T_IRQ0);

    /* 时钟中断 — 目前仅 EOI */
    if (irq == IRQ_TIMER) {
        /* TODO: 时钟计数、进程调度唤醒 */
    }

    /* 其他 IRQ — 暂时只打印 */
    if (irq != IRQ_TIMER && irq != IRQ_SPURIOUS && irq != IRQ_ERROR) {
        kprintf_color(COLOR_YELLOW, "IRQ %d\n", irq);
    }

    /* 发送 EOI (End of Interrupt) 给 LAPIC */
    lapic_eoi();
}