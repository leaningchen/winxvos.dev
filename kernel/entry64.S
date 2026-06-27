/*
 * kernel/entry64.S — BSP（Bootstrap Processor）64 位入口
 * 链接地址: 0x100000（由 linker/kernel.ld 中 .text.entry 段保证最先）
 *
 * 功能:
 *   1. 更新所有段寄存器到 64 位数据段
 *   2. 建立 BSP 内核栈（栈顶 0x580000）
 *   3. 清零 BSS 段
 *   4. 调用 kernel_main(zenith_boot_info *)
 */

    .section .text.entry, "ax"
    .code64
    .global kernel_entry

kernel_entry:
    /* 更新数据段寄存器到 64 位数据段（选择子 0x20）*/
    movw    $0x20, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    xorw    %ax, %ax
    movw    %ax, %fs
    movw    %ax, %gs

    /* 启用 SSE/SSE2（clang -O2 可能生成 SIMD 指令）
     * CR4.OSFXSR(bit9)=1, CR4.OSXMMEXCPT(bit10)=1
     * CR0.EM(bit2)=0, CR0.MP(bit1)=1 */
    movq    %cr0, %rax
    andq    $~(1<<2), %rax      /* clear CR0.EM */
    orq     $(1<<1), %rax       /* set   CR0.MP */
    movq    %rax, %cr0
    movq    %cr4, %rax
    orq     $(3<<9), %rax       /* set CR4.OSFXSR | CR4.OSXMMEXCPT */
    movq    %rax, %cr4

    /* 建立 BSP 内核栈，16 字节对齐 */
    movq    $0x580000, %rsp
    andq    $~0xF, %rsp

    /* 清零 BSS 段（REP STOSQ：8 字节/次）*/
    movq    $__bss_start, %rdi
    movq    $__bss_end,   %rcx
    subq    %rdi,         %rcx
    shrq    $3,           %rcx         /* 转换为 qword 计数 */
    xorq    %rax,         %rax
    rep stosq

    /* 调用 C 主函数 kernel_main(zenith_boot_info *info)
     * System V AMD64 ABI: 第一参数通过 RDI 传递
     * zenith_boot_info 固定在物理地址 0x5000
     */
    movq    $0x5000, %rdi
    call    kernel_main

    /* 若 kernel_main 意外返回，永久挂起 */
.Lhalt:
    cli
    hlt
    jmp     .Lhalt
