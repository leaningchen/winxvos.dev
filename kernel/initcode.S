/*===========================================================================
 * kernel/initcode.S — 第一个用户进程的内联初始化代码（占位版本）
 *
 * 此文件作为内核镜像的一部分被编译嵌入，由 userinit() 映射到
 * 第一个进程的地址空间（虚拟地址 0）。
 *
 * 当前为占位版本：仅执行一个 exec 系统调用（调用 /init），
 * 若失败则进入无限循环。批次7中将替换为真正的 initcode。
 *
 * 汇编后由 linker/initcode.ld 链接为平坦二进制，再通过
 * objcopy 嵌入内核 ELF（生成 _binary_initcode_start/size 符号）。
 *===========================================================================*/

/*
 * x86-64 系统调用约定（Linux ABI，WinixOS 也采用相同约定）：
 *   syscall 号 → %rax
 *   arg1      → %rdi
 *   arg2      → %rsi
 *   arg3      → %rdx
 *   返回值    → %rax
 *
 * 系统调用号（与 kernel/syscall.h 一致，批次3定义）：
 *   SYS_exec = 11
 */

    .text
    .globl _start
_start:
    /* exec("/init", argv) 系统调用
     * arg1 (%rdi) = 指向字符串 "/init" 的指针（相对当前地址）
     * arg2 (%rsi) = 指向 argv 的指针
     * %rax        = SYS_exec = 11
     */
    leaq    init_path(%rip), %rdi   /* 文件路径 */
    leaq    argv(%rip),      %rsi   /* 参数数组 */
    movq    $7,              %rax   /* SYS_exec = 7 */
    syscall

    /* exec 失败（理论上不应发生），进入无限循环 */
bad:
    movq    $1,  %rax   /* SYS_exit = 1 */
    movq    $1,  %rdi   /* 退出码 1 */
    syscall
    jmp     bad

    .data
init_path:
    .asciz  "/init"

    /* argv = { &"/init", NULL } */
    .align  8
argv:
    .quad   init_path
    .quad   0
