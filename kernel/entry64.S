/*===========================================================================
 * kernel/entry64.S — BSP（Bootstrap Processor）64 位内核入口
 *
 * 链接地址: KERNBASE + 0x100000 = 0xFFFF800000100000
 * 加载地址: 0x100000（bootloader 将内核加载到物理地址 1MB 处）
 *
 * bootloader 接手条件：
 *   - 已进入 64 位长模式
 *   - CR3 = 0x70000（bootloader 的临时页表，只有低地址恒等映射）
 *   - 当前 RIP = 0x100000（物理地址，链接地址此时无法直接访问）
 *
 * 本文件完成以下工作（分两阶段）：
 *
 * 【阶段一：物理地址阶段（RIP 在低地址 0x100000 附近运行）】
 *   1. 更新数据段寄存器到 64 位选择子
 *   2. 建立静态临时页表（entry_pml4），增加：
 *      - 高地址映射：VA[KERNBASE, KERNBASE+4GB) → PA[0, 4GB)（2MB大页）
 *        使用 bootloader 遗留的 PD 页面，只需新建一个 PDPT
 *      - 保留低地址恒等映射（VA=PA，供跳转前的 RIP 继续运行）
 *   3. 将 entry_pml4 物理地址写入 CR3
 *   4. 使用绝对跳转跳转到高地址标签 entry_high
 *
 * 【阶段二：高地址阶段（RIP 在 0xFFFF800000100000+ 运行）】
 *   5. 更新 RSP 到高地址内核栈
 *   6. 清零 BSS 段
 *   7. 调用 kernel_main(BootInfo *)
 *
 * 临时页表布局（使用 bootloader 遗留的 PD 页面，节省内存）：
 *   entry_pml4[0x000]   → boot_pdpt（低地址，bootloader 的 0x71000）
 *   entry_pml4[0x100]   → entry_kpdpt（高地址 KERNBASE 的 PML4 slot 256）
 *   entry_kpdpt[0..3]   → boot_pd0..pd3（复用 bootloader 的 2MB 大页 PD）
 *
 * 注意：
 *   - entry_pml4 是内核静态数据段（VMA=高地址，LMA=低地址物理地址）
 *   - 访问 entry_pml4 时必须用 %rip 相对寻址或直接物理地址
 *   - KERNBASE 在汇编中定义为立即数
 *===========================================================================*/

    .section .text.entry, "ax"
    .code64
    .global kernel_entry

/* 物理加载地址（用于汇编计算）*/
#define KERN_LOAD_PADDR  0x100000

/* 内核虚拟基址（与 memlayout.h 一致）*/
#define KERNBASE_LO      0x00000000          /* KERNBASE 低32位 */
#define KERNBASE_HI      0xFFFF8000          /* KERNBASE 高32位 */

/* bootloader 建立的页表物理地址（与 stage2.S 一致）*/
#define BOOT_PML4        0x70000
#define BOOT_PDPT        0x71000
#define BOOT_PD0         0x72000
#define BOOT_PD1         0x73000
#define BOOT_PD2         0x74000
#define BOOT_PD3         0x75000

/* KERNBASE 在 PML4 中的 slot 索引
 * PX(3, KERNBASE) = (0xFFFF800000000000 >> 39) & 0x1FF = 256 */
#define KERN_PML4_IDX    256

/* PTE 标志：Present + Writable + Page Size (2MB) */
#define PTE_PW           0x3    /* P + W（用于非叶级表项）*/
#define PTE_PWS          0x83   /* P + W + PS（2MB 大页叶表项）*/

kernel_entry:
    /*=====================================================================
     * 阶段一：物理地址阶段
     * 此时 RIP = 0x100000（物理地址），但链接地址是高地址
     * 所有指令都通过 RIP 相对寻址访问数据，编译器/汇编器处理偏移量
     *=====================================================================*/

    /* 1. 更新数据段寄存器到 64 位数据段（选择子 0x20，与 bootloader GDT 一致）*/
    movw    $0x20, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    xorw    %ax, %ax
    movw    %ax, %fs
    movw    %ax, %gs

    /* 2. 启用 SSE/SSE2（clang -O2 可能生成 SIMD 指令）
     * CR4.OSFXSR(bit9)=1, CR4.OSXMMEXCPT(bit10)=1
     * CR0.EM(bit2)=0, CR0.MP(bit1)=1 */
    movq    %cr0, %rax
    andq    $~(1<<2), %rax      /* clear CR0.EM */
    orq     $(1<<1), %rax       /* set   CR0.MP */
    movq    %rax, %cr0
    movq    %cr4, %rax
    orq     $(3<<9), %rax       /* set CR4.OSFXSR | CR4.OSXMMEXCPT */
    movq    %rax, %cr4

    /*------------------------------------------------------------------
     * 3. 建立临时高地址页表
     *
     * 策略：复用 bootloader 在 0x70000~0x75000 建立的页表结构，
     *       只需建立高地址 KERNBASE 的 PML4 entry 和新 PDPT。
     *
     * entry_pml4 是内核 .data 段中的静态数组，LMA（物理地址）= VMA - KERNBASE。
     * 用 RIP 相对寻址访问时自动得到正确的物理地址偏移。
     *
     * 但由于链接地址是高地址，leaq/movq entry_pml4(%rip) 实际上
     * 会得到高地址虚拟地址，此时高地址页表还没建好，无法解引用。
     * 因此使用手动计算物理地址的方式。
     *------------------------------------------------------------------*/

    /* 计算 entry_pml4 / entry_kpdpt 的物理地址
     *
     * 此时 RIP 在低地址（物理地址 ~0x100000），leaq 的 RIP 相对偏移
     * 由链接器按 VMA 差值写入，但实际 RIP 是低地址，因此：
     *   RDI = RIP_phys + (entry_pml4_VMA - leaq_next_VMA)
     *       = RIP_phys + (entry_pml4_VMA - leaq_next_VMA)
     *
     * 由于 VMA 和 LMA 的差值相同（都是 KERNBASE），偏移量不变，
     * leaq 直接给出正确的物理地址，无需再减 KERNBASE。
     */
    leaq    entry_pml4(%rip), %rdi      /* rdi = entry_pml4 的物理地址 */
    leaq    entry_kpdpt(%rip), %rsi     /* rsi = entry_kpdpt 的物理地址 */

    /* 清零 entry_pml4（512项 * 8字节 = 4096字节）
     * rep stosq 隐式使用 RDI，清零后 RDI 指向 entry_pml4 末尾，需恢复 */
    pushq   %rdi                        /* 保存 entry_pml4 起始地址 */
    movq    $512, %rcx
    xorq    %rax, %rax
    rep stosq
    popq    %rdi                        /* 恢复 rdi = entry_pml4 */

    /* 清零 entry_kpdpt
     * 用 RSI 作为目标：先临时借用 RDI，清零后再恢复 RSI */
    pushq   %rsi                        /* 保存 entry_kpdpt 起始地址 */
    movq    %rsi, %rdi                  /* rdi = entry_kpdpt 起始 */
    movq    $512, %rcx
    xorq    %rax, %rax
    rep stosq
    popq    %rsi                        /* 恢复 rsi = entry_kpdpt 起始 */
    leaq    entry_pml4(%rip), %rdi      /* 恢复 rdi = entry_pml4 起始 */

    /*-- entry_pml4[0] → bootloader 低地址 PDPT（恒等映射，供 RIP 继续运行）--*/
    movq    $(BOOT_PDPT | PTE_PW), %rax
    movq    %rax, (%rdi)                /* entry_pml4[0] */

    /*-- entry_pml4[256] → entry_kpdpt（高地址 KERNBASE 区域）--*/
    movq    %rsi, %rax
    orq     $PTE_PW, %rax
    movq    %rax, (KERN_PML4_IDX * 8)(%rdi)  /* entry_pml4[256] */

    /*-- entry_kpdpt[0..3] → bootloader 的 4 个 PD（2MB 大页，覆盖 0~4GB）--*/
    movq    $(BOOT_PD0 | PTE_PW), %rax
    movq    %rax, 0(%rsi)               /* entry_kpdpt[0] → PA 0~1GB */
    movq    $(BOOT_PD1 | PTE_PW), %rax
    movq    %rax, 8(%rsi)               /* entry_kpdpt[1] → PA 1~2GB */
    movq    $(BOOT_PD2 | PTE_PW), %rax
    movq    %rax, 16(%rsi)              /* entry_kpdpt[2] → PA 2~3GB */
    movq    $(BOOT_PD3 | PTE_PW), %rax
    movq    %rax, 24(%rsi)              /* entry_kpdpt[3] → PA 3~4GB */

    /* 4. 将 entry_pml4 的物理地址写入 CR3（刷新 TLB，启用新映射）*/
    movq    %rdi, %rax
    movq    %rax, %cr3

    /* 5. 绝对跳转到高地址（链接地址，即 VMA）
     * 此时高地址映射已建立，可以安全跳转 */
    movabsq $entry_high, %rax
    jmpq    *%rax

    /*=====================================================================
     * 阶段二：高地址阶段
     * RIP 现在在高地址（0xFFFF800000100000+）运行
     *=====================================================================*/
    .code64
entry_high:
    /* 调试：向 0xE9 端口写入 'H' 确认已到达高地址 */
    movb    $'H', %al
    outb    %al, $0xE9

    /* 6. 更新数据段寄存器（保持与阶段一一致，但此后段基址由 MSR 控制）*/
    movw    $0x20, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss

    /* 7. 建立高地址内核栈（固定物理地址 0x580000，对应高地址 KERNBASE+0x580000）
     * 注意：此时 kalloc 还未初始化，使用固定物理地址的栈 */
    movabsq $(0xFFFF800000580000), %rsp
    andq    $~0xF, %rsp                 /* 16 字节对齐 */

    /* 8. 清零 BSS 段（REP STOSQ：8 字节/次）
     * __bss_start 和 __bss_end 是高地址虚拟地址 */
    movabsq $__bss_start, %rdi
    movabsq $__bss_end,   %rcx
    subq    %rdi, %rcx
    shrq    $3, %rcx                    /* 转换为 qword 计数 */
    xorq    %rax, %rax
    rep stosq

    /* 9. 调用 C 主函数 kernel_main(BootInfo *info)
     * System V AMD64 ABI: 第一参数通过 RDI 传递
     * BootInfo 固定在物理地址 0x5000，对应高地址 KERNBASE+0x5000 */
    movabsq $(0xFFFF800000005000), %rdi
    call    kernel_main

    /* 若 kernel_main 意外返回，永久挂起 */
.Lhalt:
    cli
    hlt
    jmp     .Lhalt

/*===========================================================================
 * 静态临时页表（位于 .data 段，LMA=物理地址，VMA=高地址）
 *
 * 必须 4KB 对齐，因为 CR3 要求页表起始地址按 4KB 对齐。
 * 使用 .section .data.pagetable 以便 kernel.ld 将其放在 .data 开头
 * （确保地址可以被 entry64.S 正确计算）。
 *===========================================================================*/
    .section .data.pagetable, "aw"
    .align  4096
entry_pml4:
    .space  4096    /* PML4 根页表（512项 × 8字节）*/

    .align  4096
entry_kpdpt:
    .space  4096    /* 高地址 PDPT（KERNBASE 区域使用）*/
