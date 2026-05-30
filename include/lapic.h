#ifndef __LAPIC_H__
#define __LAPIC_H__

#include <types.h>

/*
 * Local APIC (LAPIC) - per-CPU interrupt controller
 *
 * Key registers:
 *   0x020: LAPIC ID
 *   0x080: Task Priority Register (TPR)
 *   0x0B0: End of Interrupt (EOI)
 *   0x0F0: Spurious Interrupt Vector Register (SVR)
 *   0x320: LVT Timer Register
 *   0x380: Timer Initial Count Register
 *   0x390: Timer Current Count Register
 *   0x3E0: Timer Divide Configuration Register
 */

/* LAPIC register offsets */
#define LAPIC_ID        0x020
#define LAPIC_TPR       0x080
#define LAPIC_EOI       0x0B0
#define LAPIC_SVR       0x0F0
#define LAPIC_ISR0      0x100
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360
#define LAPIC_LVT_ERROR 0x370
#define LAPIC_TIMER_ICR 0x380   /* Initial Count */
#define LAPIC_TIMER_CCR 0x390   /* Current Count */
#define LAPIC_TIMER_DCR 0x3E0   /* Divide Configuration */

/* LAPIC SVR bits */
#define LAPIC_SVR_ENABLE  (1 << 8)   /* Software enable bit */
#define LAPIC_SVR_VECTOR  0xFF       /* Spurious vector (use 0xFF) */

/* LVT Timer bits */
#define LAPIC_LVT_MASKED  (1 << 16)  /* Interrupt mask */
#define LAPIC_LVT_PERIODIC (1 << 17) /* Periodic mode */
#define LAPIC_LVT_ONESHOT  0         /* One-shot mode */

/* Timer divide values */
#define LAPIC_TIMER_DIV_1    0x0B    /* Divide by 1 */
#define LAPIC_TIMER_DIV_2    0x00    /* Divide by 2 */
#define LAPIC_TIMER_DIV_4    0x01    /* Divide by 4 */
#define LAPIC_TIMER_DIV_8    0x02    /* Divide by 8 */
#define LAPIC_TIMER_DIV_16   0x03    /* Divide by 16 */
#define LAPIC_TIMER_DIV_32   0x04    /* Divide by 32 (or 128) */
#define LAPIC_TIMER_DIV_64   0x05    /* Divide by 64 */
#define LAPIC_TIMER_DIV_128  0x06    /* Divide by 128 */
#define LAPIC_TIMER_DIV_256  0x07    /* Divide by 256 */

/* MSR for LAPIC base address */
#define MSR_IA32_APIC_BASE  0x1B
#define APIC_BASE_ENABLE    (1 << 11)  /* Global enable */

/* LAPIC base address (default) */
#define LAPIC_DEFAULT_BASE  0xFEE00000UL

/* Initialize the LAPIC: enable, configure timer, mask LINTs */
void lapic_init(void);

/* Send End of Interrupt to LAPIC */
void lapic_eoi(void);

/* Get LAPIC base address */
uint64_t lapic_get_base(void);

#endif /* __LAPIC_H__ */