/*
 * kernel/ap_trampoline.S — AP 蹦床代码
 * 链接地址: 0x8000（SIPI vector = 0x08，物理地址 = 0x08 × 4096 = 0x8000）
 *
 * AP 收到 SIPI 后从 0x8000 以 16 位实模式开始执行。
 * 蹦床代码必须完全自包含，不依赖任何外部符号。
 *
 * 执行流程:
 *   [.code16] AP 实模式:
 *     - 加载 BSP 已建好的 GDT（固定地址）
 *     - 进入 32 位保护模式
 *   [.code32] AP 保护模式:
 *     - 复用 BSP 已建好的页表（CR3=0x400000）
 *     - 进入 64 位长模式
 *   [.code64] AP 长模式:
 *     - 更新段寄存器
 *     - 读取 LAPIC ID，计算独立栈地址
 *     - 原子递增 cpu_count
 *     - 进入 HLT 等待循环
 */

.equ PAGE_TABLE_BASE,   0x70000     /* 与 stage2 setup_paging 一致（PML4 at 0x70000）*/
.equ AP_STACK_BASE,     0x9000      /* 每核 4KB，按 LAPIC ID 索引 */
.equ AP_STACK_SIZE,     0x1000      /* 4KB 每核 */
.equ LAPIC_BASE,        0xFEE00000  /* LAPIC 默认 MMIO 基地址 */
.equ LAPIC_ID_REG,      0x020       /* LAPIC ID 寄存器偏移 */
.equ SMP_CPU_COUNT,     0x57FC      /* cpu_count 原子计数器物理地址 */

/* GDT 描述符指针（由 Stage2 在 0x10000 + gdt_ptr 偏移处建立）
 * 硬编码 Stage2 中 gdt_ptr 的物理地址
 * gdt_ptr 在 stage2.S 中偏移约 0x100 左右，实际由链接器决定
 * 使用硬编码的 GDT 内容，在蹦床内部重新定义 */

    .section .text
    .code16
    .global ap_start

ap_start:
    cli
    cld

    /* 规范化段寄存器 */
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss

    /* 临时栈（蹦床顶部，不与其他数据冲突）*/
    movw    $0x8FFE, %sp

    /* 加载蹦床内自定义的 GDT（与 Stage2 GDT 内容相同）*/
    lgdt    ap_gdtr - ap_start + 0x8000

    /* 进入 32 位保护模式 */
    movl    %cr0, %eax
    orl     $0x1, %eax
    movl    %eax, %cr0

    /* 远跳到 AP 保护模式入口（段内地址必须用物理地址）*/
    ljmp    $0x08, $(ap_pm32 - ap_start + 0x8000)

/*---------------------------------------------------------------------------
 * AP GDT（与 Stage2 GDT 相同内容，自包含于蹦床）
 *---------------------------------------------------------------------------*/
    .align 8
ap_gdt_start:
    .quad   0x0000000000000000      /* NULL */
    /* 32 位代码段 0x08 */
    .word 0xFFFF, 0x0000; .byte 0x00, 0x9A, 0xCF, 0x00
    /* 32 位数据段 0x10 */
    .word 0xFFFF, 0x0000; .byte 0x00, 0x92, 0xCF, 0x00
    /* 64 位代码段 0x18 */
    .word 0x0000, 0x0000; .byte 0x00, 0x9A, 0xA0, 0x00
    /* 64 位数据段 0x20 */
    .word 0xFFFF, 0x0000; .byte 0x00, 0x92, 0xCF, 0x00
ap_gdt_end:

ap_gdtr:
    .word   ap_gdt_end - ap_gdt_start - 1
    .long   ap_gdt_start - ap_start + 0x8000    /* 物理地址 */

/* AP 64 位远跳目标 */
    .align 4
ap_jump64:
    .long   ap_entry64 - ap_start + 0x8000      /* 64 位入口地址 */
    .word   0x18                                 /* 64 位代码段选择子 */

/*===========================================================================
 * AP 32 位保护模式
 *===========================================================================*/
    .code32

ap_pm32:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs
    movl    $0x8FFE, %esp

    /* 复用 BSP 已建立的页表（无需重建）*/
    movl    $PAGE_TABLE_BASE, %eax
    movl    %eax, %cr3

    /* CR4.PAE = 1 */
    movl    %cr4, %eax
    orl     $(1 << 5), %eax
    movl    %eax, %cr4

    /* IA32_EFER.LME = 1 */
    movl    $0xC0000080, %ecx
    rdmsr
    orl     $(1 << 8), %eax
    wrmsr

    /* CR0.PG = 1 */
    movl    %cr0, %eax
    orl     $0x80000001, %eax
    movl    %eax, %cr0

    /* 远跳到 64 位代码段 */
    ljmpl   *(ap_jump64 - ap_start + 0x8000)

/*===========================================================================
 * AP 64 位长模式
 *===========================================================================*/
    .code64

ap_entry64:
    /* 更新段寄存器 */
    movw    $0x20, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    xorw    %ax, %ax
    movw    %ax, %fs
    movw    %ax, %gs

    /* 启用 SSE（与 BSP entry64.S 保持一致）*/
    movq    %cr0, %rax
    andq    $~(1<<2), %rax
    orq     $(1<<1), %rax
    movq    %rax, %cr0
    movq    %cr4, %rax
    orq     $(3<<9), %rax
    movq    %rax, %cr4

    /* 读取 LAPIC ID（MMIO 地址 0xFEE00020，bit31:24）*/
    movq    $(LAPIC_BASE + LAPIC_ID_REG), %rax
    movl    (%rax), %eax
    shrl    $24, %eax               /* LAPIC ID 在 bit31:24 */

    /* 分配独立栈: AP_STACK_BASE + lapic_id * AP_STACK_SIZE + AP_STACK_SIZE */
    movq    $AP_STACK_BASE, %rsp
    movq    %rax, %rcx
    imulq   $AP_STACK_SIZE, %rcx
    addq    %rcx, %rsp
    addq    $AP_STACK_SIZE, %rsp    /* 指向栈顶 */
    andq    $~0xF, %rsp             /* 16 字节对齐 */

    /* 原子递增 cpu_count（物理地址 0x57FC）*/
    movq    $SMP_CPU_COUNT, %rax
    lock incl (%rax)

    /* AP 等待循环（未来由调度器替换为任务等待）*/
ap_idle:
    hlt
    jmp     ap_idle
