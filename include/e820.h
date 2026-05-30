#ifndef __E820_H__
#define __E820_H__

#include <types.h>

#define E820_USABLE     1   /* 可用内存 */
#define E820_RESERVED   2   /* 硬件保留 */
#define E820_ACPI_RCL   3   /* ACPI 可回收 */
#define E820_ACPI_NVS   4   /* ACPI NVS */
#define E820_BAD        5   /* 损坏内存 */

typedef struct {
    uint64_t base;      /* 区域物理起始地址 */
    uint64_t length;    /* 区域长度（字节）*/
    uint32_t type;      /* 类型（见上方宏定义）*/
    uint32_t acpi_ext;  /* ACPI 3.0 扩展属性 */
} __attribute__((packed)) E820Entry;

#endif /* __E820_H__ */
