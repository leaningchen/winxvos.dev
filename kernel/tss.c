/*===========================================================================
 * kernel/tss.c — x86-64 任务状态段（TSS）及 GDT 管理
 *
 * 职责：
 *   1. 重建内核 GDT（扩展 bootloader 的5项 GDT，增加用户段和 TSS 描述符）
 *   2. 初始化 TSS 并将其描述符写入 GDT
 *   3. 加载新 GDT（lgdt）并设置 TSS 段寄存器（ltr）
 *   4. 提供 tss_set_rsp0() 供进程切换时更新内核栈顶
 *
 * GDT 布局（本文件建立后）：
 *   Index  Offset  内容
 *     0    0x00    空描述符
 *     1    0x08    32位代码段（bootloader 遗留，内核不再使用）
 *     2    0x10    32位数据段（bootloader 遗留，内核不再使用）
 *     3    0x18    64位代码段，CPL=0（内核代码）
 *     4    0x20    64位数据段，CPL=0（内核数据）
 *     5    0x28    64位代码段，CPL=3（用户代码，批次3 SYSRET 使用）
 *     6    0x30    64位数据段，CPL=3（用户数据）
 *     7    0x38    TSS 低64位描述符（系统段，占2个slot）
 *     8    0x40    TSS 高64位描述符（基址高32位）
 *
 * TSS 描述符特殊性：64 位 TSS 描述符占 16 字节（两个 GDT slot），
 * 高 8 字节存储基址的 bits[63:32]。
 *
 * 参考：
 *   - Intel SDM Vol.3 Section 7.2（TSS in 64-Bit Mode）
 *   - xv6 vm.c seginit()
 *===========================================================================*/

#include <types.h>
#include <string.h>
#include <param.h>
#include <mmu.h>
#include <tss.h>
#include <x86_64.h>

/*---------------------------------------------------------------------------
 * GDT — 全局描述符表（9个条目 = 8 * 8 + 1 * 16 字节）
 *
 * 条目7+8 合并为一个 TSS 描述符（16字节）。
 * 用 uint64_t 数组存储，条目7和8分别为 TSS 描述符的低/高64位。
 *---------------------------------------------------------------------------*/
#define GDT_ENTRIES  9    /* 7个普通段 + 1个16字节TSS描述符（占2个slot）*/

static uint64_t gdt[GDT_ENTRIES];

/*---------------------------------------------------------------------------
 * BSP 的 TSS（每 CPU 一个，多核时在 proc.c 中为 AP 分配）
 *---------------------------------------------------------------------------*/
static struct tss64 bsp_tss;

/*---------------------------------------------------------------------------
 * make_seg_desc — 构造一个 64 位段描述符（普通代码/数据段）
 *
 * 在 64 位长模式下，代码/数据段的 base 和 limit 字段被硬件忽略（平坦模型），
 * 但仍需按格式填写。
 *
 * @type:  段类型（低4位，如 SEG_CODE_EXRD / SEG_DATA_RW）
 * @dpl:   特权级（0=内核, 3=用户）
 * @longm: 是否为64位长模式代码段（L位，只对代码段有效）
 *
 * 返回：uint64_t 段描述符值
 *---------------------------------------------------------------------------*/
static uint64_t make_seg_desc(uint8_t type, uint8_t dpl, int longm)
{
    /*
     * 64位平坦段描述符格式（base=0, limit=0xFFFFF, G=1）：
     *   [15:0]   limit[15:0]  = 0xFFFF
     *   [39:16]  base[23:0]   = 0
     *   [43:40]  type         = type
     *   [44]     S=1          （代码/数据段）
     *   [46:45]  DPL          = dpl
     *   [47]     P=1          （Present）
     *   [51:48]  limit[19:16] = 0xF
     *   [52]     AVL=0
     *   [53]     L            = longm（代码段）
     *   [54]     D/B=0        （64位模式）
     *   [55]     G=1          （4KB 粒度）
     *   [63:56]  base[31:24]  = 0
     */
    uint64_t desc = 0;
    desc |= 0x0000FFFFULL;                         /* limit[15:0] */
    desc |= ((uint64_t)type)      << 40;           /* type[43:40] */
    desc |= (1ULL)                << 44;           /* S=1 */
    desc |= ((uint64_t)(dpl & 3)) << 45;           /* DPL */
    desc |= (1ULL)                << 47;           /* P=1 */
    desc |= 0xFULL                << 48;           /* limit[19:16] */
    desc |= ((uint64_t)longm)     << 53;           /* L bit */
    desc |= (1ULL)                << 55;           /* G=1 */
    return desc;
}

/*---------------------------------------------------------------------------
 * make_tss_desc — 构造 64 位 TSS 描述符（低/高各64位）
 *
 * TSS 描述符是"系统段描述符"，S=0，类型=0x9（64-bit TSS available）。
 * 基址需要完整填入64位地址，因此高64位存储 base[63:32]。
 *
 * @tss_lo: 输出 — 低64位描述符
 * @tss_hi: 输出 — 高64位描述符
 * @base:   TSS 结构体的虚拟地址
 * @size:   TSS 结构体大小（sizeof(struct tss64) - 1）
 *---------------------------------------------------------------------------*/
static void make_tss_desc(uint64_t *tss_lo, uint64_t *tss_hi,
                           uint64_t base, uint16_t size)
{
    /*
     * 低64位 TSS 描述符格式：
     *   [15:0]   limit[15:0]
     *   [31:16]  base[15:0]
     *   [39:32]  base[23:16]
     *   [43:40]  type = 0x9（64-bit TSS Available）
     *   [44]     S=0（系统段）
     *   [46:45]  DPL=0
     *   [47]     P=1
     *   [51:48]  limit[19:16]
     *   [55:52]  AVL/L/DB/G = 0
     *   [63:56]  base[31:24]
     *
     * 高64位：
     *   [31:0]   base[63:32]
     *   [63:32]  reserved（必须为0）
     */
    uint64_t lo = 0;
    lo |= (uint64_t)(size & 0xFFFF);               /* limit[15:0] */
    lo |= (base & 0xFFFFULL)         << 16;        /* base[15:0] */
    lo |= ((base >> 16) & 0xFFULL)   << 32;        /* base[23:16] */
    lo |= (uint64_t)SEG_SYS_TSS64_AVAIL << 40;    /* type=0x9 */
    /* S=0 (system segment), DPL=0 */
    lo |= (1ULL)                     << 47;        /* P=1 */
    lo |= (uint64_t)((size >> 16) & 0xF) << 48;   /* limit[19:16] */
    lo |= ((base >> 24) & 0xFFULL)   << 56;        /* base[31:24] */

    uint64_t hi = (base >> 32) & 0xFFFFFFFFULL;    /* base[63:32] */

    *tss_lo = lo;
    *tss_hi = hi;
}

/*---------------------------------------------------------------------------
 * tss_init — 初始化 GDT 和 TSS，加载到 CPU
 *
 * 调用时机：idt_init() 之后，调度器启动之前。
 * 效果：重建 GDT（添加用户段和 TSS 描述符），lgdt + ltr。
 *---------------------------------------------------------------------------*/
void tss_init(void)
{
    /* 清零 GDT 和 TSS */
    memset(gdt, 0, sizeof(gdt));
    memset(&bsp_tss, 0, sizeof(bsp_tss));

    /* --- 填充 GDT 各段描述符 --- */

    /* 0x00: 空描述符（必须）*/
    gdt[0] = 0;

    /* 0x08: 32位代码段（bootloader 遗留，不再使用但保留格式）*/
    gdt[1] = 0x00CF9A000000FFFFULL;

    /* 0x10: 32位数据段（bootloader 遗留）*/
    gdt[2] = 0x00CF92000000FFFFULL;

    /* 0x18: 64位代码段，CPL=0（KERN_CODE_SEL）*/
    gdt[3] = make_seg_desc(SEG_CODE_EXRD, 0, 1);

    /* 0x20: 64位数据段，CPL=0（KERN_DATA_SEL）*/
    gdt[4] = make_seg_desc(SEG_DATA_RW, 0, 0);

    /* 0x28: 64位代码段，CPL=3（USER_CODE_SEL，供 SYSRET 使用）
     * 注意：SYSRET 要求用户代码段在 GDT 中的位置 = (STAR MSR >> 48) + 16
     *       且用户数据段紧随其后，这个顺序由批次3 syscall_entry.S 确认。*/
    gdt[5] = make_seg_desc(SEG_CODE_EXRD, 3, 1);

    /* 0x30: 64位数据段，CPL=3（USER_DATA_SEL）*/
    gdt[6] = make_seg_desc(SEG_DATA_RW, 3, 0);

    /* 0x38 + 0x40: 64位 TSS 描述符（16字节，占两个slot）*/
    uint64_t tss_base = (uint64_t)&bsp_tss;
    uint16_t tss_size = sizeof(struct tss64) - 1;
    make_tss_desc(&gdt[7], &gdt[8], tss_base, tss_size);

    /* TSS 的 IOPB 偏移设为 TSS 大小（表示无 I/O 许可位图）*/
    bsp_tss.iomap_base = sizeof(struct tss64);

    /* --- 加载新 GDT --- */
    struct desc_ptr gdt_desc;
    gdt_desc.limit = sizeof(gdt) - 1;
    gdt_desc.base  = (uint64_t)gdt;
    lgdt64(&gdt_desc);

    /* --- 重新加载段寄存器（lgdt 后段寄存器缓存仍指向旧描述符）--- */
    /*
     * CS 不能直接 mov，需要通过远跳转或 retfq 来更新。
     * 这里用内联汇编构造一个 retfq 来切换到新的 KERN_CODE_SEL。
     * 其余段寄存器（DS/ES/SS）直接 mov KERN_DATA_SEL。
     * FS/GS 清零（64位模式下通过 MSR 设置，批次2实现）。
     */
    __asm__ volatile(
        /* 将新 CS 和返回地址压栈，然后 retfq 实现远返回以刷新 CS */
        "pushq %[kcs]\n\t"         /* 新代码段选择子 */
        "leaq  1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"          /* 返回地址（标签1:）*/
        "lretq\n\t"                /* 64位远返回：弹出 RIP 和 CS */
        "1:\n\t"
        /* 更新数据段寄存器 */
        "movw %[kds], %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        /* 清零 FS/GS（通过 MSR 在批次2设置）*/
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        : [kcs] "i" (KERN_CODE_SEL), [kds] "i" (KERN_DATA_SEL)
        : "rax", "memory"
    );

    /* --- 加载 TSS 段寄存器 --- */
    ltr(TSS_SEL);
}

/*---------------------------------------------------------------------------
 * tss_set_rsp0 — 更新 TSS.RSP0（内核栈顶）
 *
 * 每次进程切换时由调度器调用，将下一个进程的内核栈顶写入 TSS。
 * 这样当该进程从用户态通过 SYSCALL/中断陷入内核时，
 * 处理器会自动从 TSS.RSP0 加载正确的内核栈指针。
 *
 * @rsp0: 新进程内核栈顶虚拟地址（kstack + KSTACKSIZE）
 *---------------------------------------------------------------------------*/
void tss_set_rsp0(uint64_t rsp0)
{
    bsp_tss.rsp0 = rsp0;
}
