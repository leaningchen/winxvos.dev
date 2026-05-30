#ifndef __MMU_H__
#define __MMU_H__

/*===========================================================================
 * include/mmu.h — x86-64 内存管理单元 (MMU) 相关定义
 *
 * 涵盖：
 *   - 四级页表宏（PML4 / PDPT / PD / PT）
 *   - 页表项标志位（PTE_P/W/U/PS/NX 等）
 *   - GDT 段描述符结构
 *   - 段选择子常量
 *
 * 注意：PGSIZE/PGSHIFT 已在 param.h 中定义，此处不重复。
 *
 * 参考：
 *   - Intel SDM Vol.3 Chapter 4（分页）
 *   - Linux arch/x86/include/asm/pgtable_types.h
 *===========================================================================*/

#include <types.h>

/*---------------------------------------------------------------------------
 * 页表项标志位（所有级别通用）
 *---------------------------------------------------------------------------*/
#define PTE_P    (1ULL << 0)   /* Present：页面存在于物理内存中 */
#define PTE_W    (1ULL << 1)   /* Writable：可写 */
#define PTE_U    (1ULL << 2)   /* User：用户态可访问（CPL=3）*/
#define PTE_PWT  (1ULL << 3)   /* Write-Through：写穿缓存 */
#define PTE_PCD  (1ULL << 4)   /* Cache-Disable：禁止缓存 */
#define PTE_A    (1ULL << 5)   /* Accessed：已被访问（硬件置位）*/
#define PTE_D    (1ULL << 6)   /* Dirty：已被写入（仅 PT 级）*/
#define PTE_PS   (1ULL << 7)   /* Page Size：PD 级置此位表示 2MB 大页 */
#define PTE_G    (1ULL << 8)   /* Global：TLB 全局页，进程切换不刷新 */
#define PTE_NX   (1ULL << 63)  /* No-Execute：禁止执行（需 EFER.NXE=1）*/

/*---------------------------------------------------------------------------
 * 四级页表结构的虚拟地址拆分
 *
 * 48 位虚拟地址布局（x86-64 四级分页，每级9位索引）：
 *   [47:39] PML4 index (9 bits)
 *   [38:30] PDPT index (9 bits)
 *   [29:21] PD   index (9 bits)
 *   [20:12] PT   index (9 bits)
 *   [11:0]  page offset (12 bits)
 *---------------------------------------------------------------------------*/

/* 第 l 级页表的虚拟地址索引偏移量（l=0:PT, 1:PD, 2:PDPT, 3:PML4）*/
#define PXSHIFT(l)    (12 + 9*(l))

/* 从虚拟地址 va 中提取第 l 级页表的索引（0~511）*/
#define PX(l, va)     (((uint64_t)(va) >> PXSHIFT(l)) & 0x1FFUL)

/* 每级页表的条目数（4096 / 8 = 512）*/
#define PTESZ         512

/* 从页表项中提取 4KB 对齐的物理地址（去掉低12位标志）*/
#define PTE_ADDR(pte)    ((uint64_t)(pte) & ~0xFFFULL)

/* 从页表项中提取标志位（低12位）*/
#define PTE_FLAGS(pte)   ((uint64_t)(pte) &  0xFFFULL)

/*---------------------------------------------------------------------------
 * 页表项类型别名
 *
 * 所有级别（PML4/PDPT/PD/PT）的表项都是 64 位整数，
 * 使用不同的 typedef 仅是语义上的区分。
 *---------------------------------------------------------------------------*/
typedef uint64_t pte_t;     /* 通用页表项类型（兼容旧 proc.h）*/
typedef uint64_t pml4e_t;   /* PML4 表项（Page Map Level 4 Entry）*/
typedef uint64_t pdpte_t;   /* PDPT 表项（Page Directory Pointer Table）*/
typedef uint64_t pde_t;     /* PD 表项（Page Directory）*/

/*---------------------------------------------------------------------------
 * GDT 段描述符（64位 System V ABI 使用的简化段描述符）
 *
 * 64位模式下，代码/数据段的基址和限制字段被忽略（平坦模式），
 * 只有 L（64位代码段）、DPL、P 等标志位有意义。
 *---------------------------------------------------------------------------*/

/* 段描述符标志位（高32位中的各字段）*/
#define SEG_DESCTYPE(x) ((x) << 0x04)   /* 描述符类型：0=系统, 1=代码/数据 */
#define SEG_PRES(x)     ((x) << 0x07)   /* Present（段存在）*/
#define SEG_SAVL(x)     ((x) << 0x0C)   /* Available for system use */
#define SEG_LONG(x)     ((x) << 0x0D)   /* Long mode（64位代码段）*/
#define SEG_SIZE(x)     ((x) << 0x0E)   /* Size：0=16bit/64bit, 1=32bit */
#define SEG_GRAN(x)     ((x) << 0x0F)   /* Granularity：0=1B, 1=4KB */
#define SEG_PRIV(x)     (((x) & 0x03) << 0x05) /* DPL（特权级 0-3）*/

/* 段类型（SEG_DESCTYPE=1，代码/数据段）*/
#define SEG_DATA_RD     0x00   /* 只读数据段 */
#define SEG_DATA_RW     0x02   /* 读写数据段（常用）*/
#define SEG_CODE_EX     0x08   /* 只执行代码段 */
#define SEG_CODE_EXRD   0x0A   /* 执行+可读代码段（常用）*/

/* 系统段类型（SEG_DESCTYPE=0）*/
#define SEG_SYS_TSS64_AVAIL 0x09  /* 64位 TSS（可用状态）*/
#define SEG_SYS_TSS64_BUSY  0x0B  /* 64位 TSS（忙碌状态）*/

/*---------------------------------------------------------------------------
 * 段选择子常量（与 bootloader/GDT 匹配）
 *
 * GDT 布局（bootloader 建立，内核沿用）：
 *   0x00: 空描述符
 *   0x08: 32位代码段（stage2 使用，内核 64 位后不用）
 *   0x10: 32位数据段（stage2 使用）
 *   0x18: 64位代码段（内核 CPL=0）
 *   0x20: 64位数据段（内核 CPL=0）
 *   0x28: 64位代码段（用户 CPL=3，批次3 sysret 后使用）
 *   0x30: 64位数据段（用户 CPL=3）
 *   0x38: TSS 描述符低64位（tss.c 填充，占16字节 = 2个slot）
 *---------------------------------------------------------------------------*/
#define KERN_CODE_SEL  0x18   /* 内核 64位代码段选择子 */
#define KERN_DATA_SEL  0x20   /* 内核 64位数据段选择子 */
#define USER_CODE_SEL  (0x28 | 3)  /* 用户 64位代码段（RPL=3）*/
#define USER_DATA_SEL  (0x30 | 3)  /* 用户 64位数据段（RPL=3）*/
#define TSS_SEL        0x38   /* TSS 段选择子 */

#endif /* __MMU_H__ */
