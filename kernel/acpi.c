#include <types.h>
#include "acpi.h"
#include "util.h"

/* 模块状态 */
static ACPI_RSDP *g_rsdp    = (ACPI_RSDP *)0;
static int        g_use_xsdt = 0;

/*---------------------------------------------------------------------------
 * acpi_checksum — 计算 ACPI 校验和（字节之和 mod 256 = 0）
 *---------------------------------------------------------------------------*/
static uint8_t acpi_checksum(const void *ptr, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)ptr;
    uint8_t sum = 0;
    while (len--) sum += *p++;
    return sum;
}

/*---------------------------------------------------------------------------
 * acpi_init — 初始化 ACPI 子系统
 *---------------------------------------------------------------------------*/
void acpi_init(uint64_t rsdp_phys)
{
    if (rsdp_phys == 0) return;

    g_rsdp = (ACPI_RSDP *)(uintptr_t)rsdp_phys;

    /* 验证签名和校验和 */
    if (kstrncmp(g_rsdp->signature, "RSD PTR ", 8) != 0) {
        g_rsdp = (ACPI_RSDP *)0;
        return;
    }

    if (acpi_checksum(g_rsdp, 20) != 0) {
        g_rsdp = (ACPI_RSDP *)0;
        return;
    }

    /* ACPI 2.0+：优先使用 XSDT */
    g_use_xsdt = (g_rsdp->revision >= 2 && g_rsdp->xsdt_addr != 0);
}

/*---------------------------------------------------------------------------
 * acpi_find_table — 在 RSDT/XSDT 中查找指定签名的表
 *---------------------------------------------------------------------------*/
ACPI_Header *acpi_find_table(const char *sig)
{
    if (!g_rsdp) return (ACPI_Header *)0;

    if (g_use_xsdt) {
        ACPI_XSDT *xsdt = (ACPI_XSDT *)(uintptr_t)g_rsdp->xsdt_addr;
        if (acpi_checksum(xsdt, xsdt->hdr.length) != 0)
            return (ACPI_Header *)0;

        uint32_t count = (xsdt->hdr.length - sizeof(ACPI_Header)) / 8;
        for (uint32_t i = 0; i < count; i++) {
            ACPI_Header *hdr = (ACPI_Header *)(uintptr_t)xsdt->entries[i];
            if (kstrncmp(hdr->signature, sig, 4) == 0)
                return hdr;
        }
    } else {
        ACPI_RSDT *rsdt = (ACPI_RSDT *)(uintptr_t)g_rsdp->rsdt_addr;
        if (acpi_checksum(rsdt, rsdt->hdr.length) != 0)
            return (ACPI_Header *)0;

        uint32_t count = (rsdt->hdr.length - sizeof(ACPI_Header)) / 4;
        for (uint32_t i = 0; i < count; i++) {
            ACPI_Header *hdr = (ACPI_Header *)(uintptr_t)rsdt->entries[i];
            if (kstrncmp(hdr->signature, sig, 4) == 0)
                return hdr;
        }
    }

    return (ACPI_Header *)0;
}

/*---------------------------------------------------------------------------
 * acpi_get_lapic_ids — 从 MADT 收集所有 AP 的 LAPIC ID
 *---------------------------------------------------------------------------*/
int acpi_get_lapic_ids(uint8_t *ids, int max_cnt)
{
    ACPI_MADT *madt = (ACPI_MADT *)acpi_find_table("APIC");
    if (!madt) return 0;

    /* 获取 BSP 的 LAPIC ID（从 LAPIC ID 寄存器读取）*/
    uint32_t bsp_lapic_id = *((volatile uint32_t *)(uintptr_t)
                               (acpi_get_lapic_base() + 0x20));
    bsp_lapic_id >>= 24;

    int count = 0;
    uint8_t *ptr = (uint8_t *)madt + sizeof(ACPI_MADT);
    uint8_t *end = (uint8_t *)madt + madt->hdr.length;

    while (ptr < end && count < max_cnt) {
        MADT_EntryHeader *eh = (MADT_EntryHeader *)ptr;
        if (eh->length == 0) break;   /* 防止无限循环 */

        if (eh->type == 0) {          /* Processor Local APIC */
            MADT_LocalAPIC *la = (MADT_LocalAPIC *)ptr;
            /* flags bit0=1: 处理器可用；跳过 BSP */
            if ((la->flags & 1) && la->apic_id != (uint8_t)bsp_lapic_id)
                ids[count++] = la->apic_id;
        }

        ptr += eh->length;
    }

    return count;
}

/*---------------------------------------------------------------------------
 * acpi_get_lapic_base — 获取 LAPIC MMIO 物理基地址
 *---------------------------------------------------------------------------*/
uint64_t acpi_get_lapic_base(void)
{
    /* 首先从 MADT 读取基地址 */
    ACPI_MADT *madt = (ACPI_MADT *)acpi_find_table("APIC");
    uint64_t base = madt ? madt->lapic_addr : 0xFEE00000ULL;

    if (!madt) return base;

    /* 检查是否有 Local APIC Address Override（type=5）*/
    uint8_t *ptr = (uint8_t *)madt + sizeof(ACPI_MADT);
    uint8_t *end = (uint8_t *)madt + madt->hdr.length;

    while (ptr < end) {
        MADT_EntryHeader *eh = (MADT_EntryHeader *)ptr;
        if (eh->length == 0) break;

        if (eh->type == 5) {
            MADT_LAPICOverride *ov = (MADT_LAPICOverride *)ptr;
            base = ov->lapic_addr;
            break;
        }
        ptr += eh->length;
    }

    return base;
}
