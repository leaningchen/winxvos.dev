#include <types.h>
#include <bootinfo.h>
#include <string.h>
#include "smp.h"
#include "acpi.h"

/* AP 蹦床代码在内核镜像中的位置（由链接脚本导出）*/
extern char __trampoline_start[];
extern char __trampoline_end[];

/* AP 计数器物理地址（与 ap_trampoline.S 中 SMP_CPU_COUNT 相同）*/
#define SMP_CPU_COUNT_ADDR  ((volatile uint32_t *)(uintptr_t)0x57FC)

/* AP 蹦床目标物理地址 */
#define AP_TRAMPOLINE_ADDR  0x8000ULL

/* LAPIC 寄存器偏移 */
#define LAPIC_ICR_LOW       0x300
#define LAPIC_ICR_HIGH      0x310
#define LAPIC_ICR_PENDING   (1u << 12)

/* 最大支持 AP 数 */
#define MAX_AP_COUNT        31

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
 * lapic_icr_wait — 等待 ICR Delivery Status 清零（bit12）
 *---------------------------------------------------------------------------*/
static void lapic_icr_wait(uint64_t lapic_base)
{
    uint32_t timeout = 1000000;
    while ((lapic_read(lapic_base, LAPIC_ICR_LOW) & LAPIC_ICR_PENDING)
           && timeout--)
    {
        __asm__ volatile ("pause" ::: "memory");
    }
}

/*---------------------------------------------------------------------------
 * spin_pause — 纯自旋若干次（不依赖时间，给硬件/QEMU 让出时间片）
 *---------------------------------------------------------------------------*/
static void spin_pause(uint32_t n)
{
    while (n--) {
        __asm__ volatile ("pause" ::: "memory");
    }
}

/*---------------------------------------------------------------------------
 * smp_init — 初始化 SMP，唤醒所有 AP
 *
 * 流程（Intel SDM Vol.3 Ch.8.4.4 MP 初始化协议）:
 *   1. 广播 INIT IPI → 自旋等待 ICR 发送完成
 *   2. 自旋 ~10ms 等量（500000 次 pause）
 *   3. 广播 SIPI    → 自旋等待 ICR 发送完成
 *   4. 自旋 ~200μs  等量（10000 次 pause）
 *   5. 若 AP 未全部上线，再发一次 SIPI
 *   6. 纯自旋等待 cpu_count 达到预期（最多 2000 万次 pause ≈ 若干毫秒）
 *---------------------------------------------------------------------------*/
int smp_init(zenith_boot_info *info)
{
    /* 初始化 ACPI */
    acpi_init(info->acpi_rsdp_addr);

    /* 获取 LAPIC 基地址 */
    uint64_t lapic_base = acpi_get_lapic_base();
    if (lapic_base == 0) lapic_base = 0xFEE00000ULL;

    /* 收集所有 AP 的 LAPIC ID */
    uint8_t ap_ids[MAX_AP_COUNT];
    int ap_count = acpi_get_lapic_ids(ap_ids, MAX_AP_COUNT);

    /* BSP 已在线 */
    *SMP_CPU_COUNT_ADDR = 1;
    info->cpu_count     = 1;

    if (ap_count == 0)
        return 1;

    /* 将蹦床复制到物理地址 0x8000 */
    uint64_t tramp_size = (uint64_t)(__trampoline_end - __trampoline_start);
    memcpy((void *)(uintptr_t)AP_TRAMPOLINE_ADDR,
           __trampoline_start, (size_t)tramp_size);

    uint8_t vector = (uint8_t)(AP_TRAMPOLINE_ADDR >> 12);   /* = 0x08 */
    uint32_t expected = (uint32_t)(1 + ap_count);

    /* --- 逐个发送 INIT-SIPI-SIPI（避免广播在 QEMU 下的兼容性问题）--- */
    for (int i = 0; i < ap_count; i++) {
        uint32_t dest = (uint32_t)ap_ids[i] << 24;

        /* INIT assert */
        lapic_write(lapic_base, LAPIC_ICR_HIGH, dest);
        lapic_write(lapic_base, LAPIC_ICR_LOW,  0x000C4500u);
        lapic_icr_wait(lapic_base);
    }

    /* 等待 ~10ms：给所有 AP 完成 INIT 复位（500000 次 pause）*/
    spin_pause(500000);

    /* 第一次 SIPI */
    for (int i = 0; i < ap_count; i++) {
        uint32_t dest = (uint32_t)ap_ids[i] << 24;
        lapic_write(lapic_base, LAPIC_ICR_HIGH, dest);
        lapic_write(lapic_base, LAPIC_ICR_LOW,  0x000C4600u | vector);
        lapic_icr_wait(lapic_base);
    }

    /* 等待 ~200μs（10000 次 pause）*/
    spin_pause(10000);

    /* 若 AP 尚未全部上线，发第二次 SIPI */
    if (*SMP_CPU_COUNT_ADDR < expected) {
        for (int i = 0; i < ap_count; i++) {
            uint32_t dest = (uint32_t)ap_ids[i] << 24;
            lapic_write(lapic_base, LAPIC_ICR_HIGH, dest);
            lapic_write(lapic_base, LAPIC_ICR_LOW,  0x000C4600u | vector);
            lapic_icr_wait(lapic_base);
        }
        spin_pause(10000);
    }

    /* 纯自旋等待所有 AP 上线（最多 2000 万次 pause）*/
    uint32_t spin = 20000000u;
    while (*SMP_CPU_COUNT_ADDR < expected && spin--)
        __asm__ volatile ("pause" ::: "memory");

    uint32_t online = *SMP_CPU_COUNT_ADDR;
    if (online > expected) online = expected;   /* 防止重复计数溢出 */
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
