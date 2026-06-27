#ifndef __BOOT_INFO_H__
#define __BOOT_INFO_H__

#include <types.h>

#define BOOT_INFO_MAGIC  0xB007B007U
#define BOOT_INFO_ADDR   0x5000U      /* 固定物理地址 */
#define E820_BUFFER_ADDR 0x6000U      /* E820 数组物理地址 */

/*
 * zenith_boot_info 结构体 — 固定存放于物理地址 0x5000
 * Stage2 汇编代码按偏移写入各字段，C 代码通过结构体指针访问。
 * 字段偏移必须与 boot/stage2.S 中的 .equ 常量严格对应。
 */
typedef struct {
    uint32_t magic;           /* +0x00  魔数 0xB007B007，用于校验 */
    uint32_t version;         /* +0x04  结构体版本 = 1 */
    uint64_t fb_addr;         /* +0x08  VESA LFB 物理地址 */
    uint32_t fb_pitch;        /* +0x10  每行字节数（LinBytesPerScanLine）*/
    uint32_t fb_width;        /* +0x14  水平分辨率（像素）*/
    uint32_t fb_height;       /* +0x18  垂直分辨率（像素）*/
    uint8_t  fb_bpp;          /* +0x1C  色深（32）*/
    uint8_t  _pad[3];         /* +0x1D  对齐填充 */
    uint32_t e820_count;      /* +0x20  E820 条目数量 */
    uint32_t e820_addr;       /* +0x24  E820 数组物理地址（= 0x6000）*/
    uint64_t mem_total;       /* +0x28  可用内存字节总数（type=1 累加）*/
    uint64_t acpi_rsdp_addr;  /* +0x30  ACPI RSDP 物理地址 */
    uint32_t boot_drive;      /* +0x38  BIOS 引导驱动器号 */
    uint32_t cpu_count;       /* +0x3C  CPU 逻辑核心总数（含 BSP）*/
} __attribute__((packed)) zenith_boot_info;

#endif /* __BOOT_INFO_H__ */
