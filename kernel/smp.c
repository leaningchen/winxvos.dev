#include <types.h>
#include <boot_info.h>
#include "smp.h"
#include "acpi.h"
#include "util.h"

/* AP 蹦床代码在内核镜像中的位置（由链接脚本导出）*/
extern char __trampoline_start[];
extern char __trampoline_end[];

/* AP 计数器物理地址（与 ap_trampoline.S 中 SMP_CPU_COUNT 相同）*/
#define SMP_CPU_COUNT_ADDR  ((volatile uint32_t *)(uintptr_t)0x57FC)

/* AP 蹦床目标物理地址 */
#define AP_TRAMPOLINE_ADDR  0x8000ULL

/* LAPIC 寄存器偏移 */
#define LAPIC_ID        0x020
#define LAPIC_EOI       0x0B0
#define LAPIC_ICR_LOW   0x300
#define LAPIC_ICR_HIGH  0x310
#define LAPIC_ICR_PENDING (1u << 12)

/* 最大支持 AP 数 */
#define MAX_AP_COUNT    31

/*---------------------------------------------------------------------------
 * lapic_read / lapic_write
 *---------------------------------------------------------------------------*/
static inline uint32_t lapic_read(uint64_t base, uint32_t reg)
{
    return *((volatile uint32_t *)(uintptr_t)(base + reg));
}

static inline void lapic_write(uint64_t base, uint32_t reg, uint32_t val)
{
    *((volatile uint32_t *)(uintptr_t)(base + reg)) = val;
}

/*---------------------------------------------------------------------------
 * udelay — 简单忙等延时（微秒级近似，无需精确计时）
 *---------------------------------------------------------------------------*/
static void udelay(uint32_t us)
{
    volatile uint64_t n = (uint64_t)us * 500ULL;
    while (n--) {
        __asm__ volatile ("pause" ::: "memory");
    }
}

/*---------------------------------------------------------------------------
 * lapic_icr_wait — 等待 ICR 发送完成（bit12 清零）
 *---------------------------------------------------------------------------*/
static void lapic_icr_wait(uint64_t lapic_base)
{
    uint32_t timeout = 100000;
    while ((lapic_read(lapic_base, LAPIC_ICR_LOW) & LAPIC_ICR_PENDING)
           && timeout--)
    {
        __asm__ volatile ("pause" ::: "memory");
    }
}

/*---------------------------------------------------------------------------
 * send_init_ipi — 向目标 AP 发送 INIT IPI
 *---------------------------------------------------------------------------*/
static void send_init_ipi(uint64_t lapic_base, uint8_t apic_id)
{
    /* ICR_HIGH: 目标 APIC ID（bit31:24）*/
    lapic_write(lapic_base, LAPIC_ICR_HIGH, (uint32_t)apic_id << 24);
    /* ICR_LOW: INIT, Level=Assert, Trigger=Edge, Dest=Physical */
    lapic_write(lapic_base, LAPIC_ICR_LOW, 0x000C4500u);
    lapic_icr_wait(lapic_base);
    udelay(10000);   /* 10ms */

    /* De-assert INIT */
    lapic_write(lapic_base, LAPIC_ICR_HIGH, (uint32_t)apic_id << 24);
    lapic_write(lapic_base, LAPIC_ICR_LOW,  0x000C0500u);
    lapic_icr_wait(lapic_base);
}

/*---------------------------------------------------------------------------
 * send_sipi — 向目标 AP 发送 SIPI
 * vector: AP_TRAMPOLINE_ADDR >> 12 = 0x08
 *---------------------------------------------------------------------------*/
static void send_sipi(uint64_t lapic_base, uint8_t apic_id, uint8_t vector)
{
    lapic_write(lapic_base, LAPIC_ICR_HIGH, (uint32_t)apic_id << 24);
    lapic_write(lapic_base, LAPIC_ICR_LOW,
                0x000C4600u | (uint32_t)vector);   /* SIPI */
    lapic_icr_wait(lapic_base);
    udelay(200);     /* 200μs */
}

/*---------------------------------------------------------------------------
 * smp_init — 初始化 SMP，唤醒所有 AP
 *---------------------------------------------------------------------------*/
int smp_init(BootInfo *info)
{
    /* 初始化 ACPI */
    acpi_init(info->acpi_rsdp_addr);

    /* 获取 LAPIC 基地址 */
    uint64_t lapic_base = acpi_get_lapic_base();
    if (lapic_base == 0) lapic_base = 0xFEE00000ULL;

    /* 收集所有 AP 的 LAPIC ID */
    uint8_t ap_ids[MAX_AP_COUNT];
    int ap_count = acpi_get_lapic_ids(ap_ids, MAX_AP_COUNT);

    /* 初始化 cpu_count：BSP 已在线 = 1 */
    *SMP_CPU_COUNT_ADDR = 1;
    info->cpu_count = 1;

    if (ap_count == 0) {
        /* 单核机器，直接返回 */
        return 1;
    }

    /* 将 AP 蹦床代码复制到物理地址 0x8000 */
    uint64_t tramp_size = (uint64_t)(__trampoline_end - __trampoline_start);
    kmemcpy((void *)(uintptr_t)AP_TRAMPOLINE_ADDR,
            __trampoline_start, (size_t)tramp_size);

    /* 发送 INIT-SIPI-SIPI 序列唤醒每个 AP */
    uint8_t vector = (uint8_t)(AP_TRAMPOLINE_ADDR >> 12);   /* = 0x08 */

    for (int i = 0; i < ap_count; i++) {
        send_init_ipi(lapic_base, ap_ids[i]);
        send_sipi(lapic_base, ap_ids[i], vector);
        send_sipi(lapic_base, ap_ids[i], vector);   /* 第二次 SIPI */
    }

    /* 等待所有 AP 上线（最多 200ms）*/
    uint32_t expected = (uint32_t)(1 + ap_count);
    uint32_t timeout  = 200000;   /* 200ms @ 1μs/次 */
    while (*SMP_CPU_COUNT_ADDR < expected && timeout > 0) {
        udelay(1);
        timeout--;
    }

    uint32_t online = *SMP_CPU_COUNT_ADDR;
    info->cpu_count = online;

    return (int)online;
}

/*---------------------------------------------------------------------------
 * smp_cpu_count — 读取当前在线 CPU 数
 *---------------------------------------------------------------------------*/
int smp_cpu_count(void)
{
    return (int)*SMP_CPU_COUNT_ADDR;
}
