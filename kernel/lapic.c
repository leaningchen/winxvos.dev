/*
 * lapic.c - Local APIC initialization and EOI
 *
 * Enable the LAPIC, configure the timer for periodic interrupts,
 * and provide EOI function for interrupt handlers.
 */

#include <lapic.h>
#include <x86_64.h>
#include <idt.h>
#include <stdio.h>
#include "video.h"

/* LAPIC MMIO base address (detected from MSR) */
static uint64_t lapic_base_addr = LAPIC_DEFAULT_BASE;

/*---------------------------------------------------------------------------
 * lapic_read / lapic_write - Access LAPIC MMIO registers
 *---------------------------------------------------------------------------*/
static inline uint32_t lapic_read(uint32_t reg)
{
    return *((volatile uint32_t *)(lapic_base_addr + reg));
}

static inline void lapic_write(uint32_t reg, uint32_t val)
{
    *((volatile uint32_t *)(lapic_base_addr + reg)) = val;
    /* LAPIC writes need a short delay to take effect */
    lapic_read(LAPIC_ID);  /* dummy read for sync */
}

/*---------------------------------------------------------------------------
 * lapic_get_base - Read LAPIC base address from MSR
 *---------------------------------------------------------------------------*/
uint64_t lapic_get_base(void)
{
    return lapic_base_addr;
}

/*---------------------------------------------------------------------------
 * lapic_init - Enable LAPIC and configure timer
 *
 * 1. Read base address from IA32_APIC_BASE MSR
 * 2. Software-enable the LAPIC (SVR bit 8)
 * 3. Set spurious interrupt vector
 * 4. Mask LINT0, LINT1, Error LVT
 * 5. Configure timer: periodic, vector T_IRQ0 (0x20)
 * 6. Set divide value and initial count
 *---------------------------------------------------------------------------*/
void lapic_init(void)
{
    /* Read LAPIC base from MSR */
    uint64_t msr = rdmsr(MSR_IA32_APIC_BASE);
    lapic_base_addr = msr & 0xFFFFFF000ULL;  /* bits 12-35 = base */

    /* Ensure global APIC enable (bit 11) */
    if (!(msr & APIC_BASE_ENABLE)) {
        wrmsr(MSR_IA32_APIC_BASE, msr | APIC_BASE_ENABLE);
    }

    /* Software enable: set SVR with vector 0xFF and enable bit */
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | LAPIC_SVR_VECTOR);

    /* Set Task Priority to 0 (accept all interrupts) */
    lapic_write(LAPIC_TPR, 0);

    /* Mask all non-timer LVT entries */
    lapic_write(LAPIC_LVT_LINT0, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR,  LAPIC_LVT_MASKED);

    /* Configure timer: periodic mode, vector = T_IRQ0 (32) */
    lapic_write(LAPIC_LVT_TIMER, T_IRQ0 | LAPIC_LVT_PERIODIC);

    /* Set divide configuration (divide by 16) */
    lapic_write(LAPIC_TIMER_DCR, LAPIC_TIMER_DIV_16);

    /* Set initial count (calibrated approximate value for ~100Hz in QEMU)
     * QEMU LAPIC timer frequency is typically the CPU bus frequency / divide.
     * For a reasonable tick rate, use a moderate initial count. */
    lapic_write(LAPIC_TIMER_ICR, 1000000);

    kprintf_color(COLOR_GREEN, "LAPIC    : enabled (base=%p)\n",
                  (void *)lapic_base_addr);
}

/*---------------------------------------------------------------------------
 * lapic_eoi - Send End of Interrupt
 *---------------------------------------------------------------------------*/
void lapic_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}