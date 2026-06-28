/* ============================================================================
 * kernel/entry64.S — BSP 64 位长模式入口
 * 链接地址: 0xFFFF800000100000
 *
 * 已由 setup.S 完成：
 *   - 高半核页表映射
 *   - FPU/SSE/AVX 启用
 *   - SMEP/SMAP/NX 安全特性开启
 *   - 引导信息指针通过 RDI 传递 (HHDM + BOOT_INFO)
 * ============================================================================ */

    .section .text.entry, "ax"
    .code64
    .global kernel_entry

.equ HHDM_OFFSET,    0xFFFF800000000000
.equ BSP_STACK_PHYS, 0x580000
.equ BOOT_INFO_PHYS, 0x5000

kernel_entry:
    /* 1. 段寄存器刷新 (防御性编程) */
    movw    $0x20, %ax              /* 64位数据段选择子 */
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    xorw    %ax, %ax
    movw    %ax, %fs
    movw    %ax, %gs

    cld                             /* 字符串操作正向递增 */

    /* 2. 硬件状态无需重复配置 (FPU/SSE/AVX/安全特性已由 setup.S 完成) */

    /* 3. 建立高半核内核栈，16 字节对齐 */
    movabs  $(HHDM_OFFSET + BSP_STACK_PHYS), %rsp
    andq    $~0xF, %rsp

    /* 4. 清零 BSS 段 (按字节，避免对齐问题) */
    lea     __bss_start(%rip), %rdi
    lea     __bss_end(%rip), %rsi
    subq    %rdi, %rsi              /* rsi = 字节数 */
    movq    %rsi, %rcx
    xorl    %eax, %eax              /* rax = 0 */
    rep stosb

    /* 5. RDI 已由 setup.S 设为 HHDM + BOOT_INFO，直接调用 C 入口 */
    call    zenith_entrance_main

.Lhalt:
    cli
    hlt
    jmp     .Lhalt