#ifndef __ACPI_H__
#define __ACPI_H__

#include <types.h>

/*---------------------------------------------------------------------------
 * ACPI 基础结构体
 *---------------------------------------------------------------------------*/

/* RSDP — Root System Description Pointer */
typedef struct {
    char     signature[8];   /* "RSD PTR " */
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;       /* 0=ACPI 1.0, 2+=ACPI 2.0+ */
    uint32_t rsdt_addr;      /* RSDT 物理地址（32位）*/
    /* ACPI 2.0+ 扩展字段 */
    uint32_t length;
    uint64_t xsdt_addr;      /* XSDT 物理地址（64位）*/
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} __attribute__((packed)) ACPI_RSDP;

/* 通用 ACPI 表头 */
typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) ACPI_Header;

/* RSDT — Root System Description Table（32位地址）*/
typedef struct {
    ACPI_Header hdr;
    uint32_t    entries[];   /* 可变长度，32位物理地址数组 */
} __attribute__((packed)) ACPI_RSDT;

/* XSDT — Extended System Description Table（64位地址）*/
typedef struct {
    ACPI_Header hdr;
    uint64_t    entries[];   /* 可变长度，64位物理地址数组 */
} __attribute__((packed)) ACPI_XSDT;

/* MADT — Multiple APIC Description Table */
typedef struct {
    ACPI_Header hdr;
    uint32_t    lapic_addr;  /* LAPIC 物理基地址 */
    uint32_t    flags;       /* bit0=1: 有 8259 PIC */
} __attribute__((packed)) ACPI_MADT;

/* MADT 条目通用头 */
typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) MADT_EntryHeader;

/* MADT 条目 type=0: Processor Local APIC */
typedef struct {
    uint8_t  type;           /* = 0 */
    uint8_t  length;         /* = 8 */
    uint8_t  acpi_cpu_id;
    uint8_t  apic_id;
    uint32_t flags;          /* bit0=1: 处理器可用 */
} __attribute__((packed)) MADT_LocalAPIC;

/* MADT 条目 type=5: Local APIC Address Override */
typedef struct {
    uint8_t  type;           /* = 5 */
    uint8_t  length;         /* = 12 */
    uint16_t reserved;
    uint64_t lapic_addr;     /* 覆盖 MADT.lapic_addr */
} __attribute__((packed)) MADT_LAPICOverride;

/*---------------------------------------------------------------------------
 * 接口函数
 *---------------------------------------------------------------------------*/

/* 初始化 ACPI（设置 RSDP 地址）*/
void acpi_init(uint64_t rsdp_phys);

/* 在 RSDT/XSDT 中查找指定签名的表，返回表头物理地址；未找到返回 NULL */
ACPI_Header *acpi_find_table(const char *sig);

/* 获取 MADT 中所有 Processor Local APIC 条目的 LAPIC ID 数组
 * @ids:     输出数组（调用者提供，最多 max_count 个）
 * @max_cnt: 数组最大容量
 * 返回: 实际填入数量
 */
int acpi_get_lapic_ids(uint8_t *ids, int max_cnt);

/* 获取 LAPIC MMIO 物理基地址 */
uint64_t acpi_get_lapic_base(void);

#endif /* __ACPI_H__ */
