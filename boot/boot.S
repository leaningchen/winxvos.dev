/*
 * boot/stage1.S — MBR 引导扇区
 * 链接地址: 0x7C00，输出为纯二进制，必须恰好 512 字节
 *
 * 功能:
 *   1. 规范化段寄存器
 *   2. 开启 A20 地址线（Fast A20）
 *   3. 通过 BIOS INT 13h/42h 将 Stage2 加载到 0x1000:0x0000
 *   4. 跳转到 Stage2
 */

/* Stage2 占用的扇区数，由 Makefile 通过 --defsym 传入 */
#ifndef STAGE2_SECTORS
#define STAGE2_SECTORS 64
#endif

    .code16
    .section .text
    .global _start

_start:
    /* 规范化 CS:IP = 0x0000:0x7C00 */
    ljmp $0x0000, $real_start

real_start:
    /* 关中断，清方向标志 */
    cli
    cld

    /* 规范化数据段 */
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7BFF, %sp        /* 栈向下生长，不覆盖 MBR */

    /* 保存引导驱动器号（DL 由 BIOS 传入）*/
    movb    %dl, boot_drive_save

    /* --------------------------------------------------------
     * 开启 A20 地址线（Fast A20 via port 0x92）
     * bit1=1: A20 enable
     * bit0=0: 不触发系统复位
     * -------------------------------------------------------- */
    inb     $0x92, %al
    orb     $0x02, %al
    andb    $0xFE, %al          /* 确保 bit0 = 0 */
    outb    %al, $0x92

    /* --------------------------------------------------------
     * 加载 Stage2 到物理地址 0x10000（段 0x1000，偏移 0x0000）
     * 使用 INT 13h/AH=42h 扩展读（LBA 模式）
     * -------------------------------------------------------- */
    movb    boot_drive_save, %dl
    movw    $dap, %si           /* DS:SI → DAP */
    movb    $0x42, %ah
    int     $0x13
    jc      disk_error

    /* 跳转到 Stage2 */
    ljmp    $0x1000, $0x0000

    /* 不应返回此处 */
halt:
    cli
    hlt
    jmp     halt

/* --------------------------------------------------------
 * 磁盘错误处理：显示 'E' 后停机
 * -------------------------------------------------------- */
disk_error:
    movb    $'E', %al
    call    print_char
    jmp     halt

/* --------------------------------------------------------
 * print_char: 通过 BIOS INT 10h/AH=0x0E 打印 AL 中的字符
 * -------------------------------------------------------- */
print_char:
    movb    $0x0E, %ah
    movw    $0x0007, %bx        /* BH=0（页），BL=7（浅灰）*/
    int     $0x10
    ret

/* --------------------------------------------------------
 * DAP（Disk Address Packet）— 16 字节，用于 INT 13h/42h
 * -------------------------------------------------------- */
    .balign 16              /* 强制 16 字节对齐，规避现代 CSM 的 Block I/O 模拟 Bug */
dap:
    .byte   0x10                /* DAP 大小 = 16 字节 */
    .byte   0x00                /* 保留 */
    .word   STAGE2_SECTORS      /* 读取扇区数 */
    .word   0x0000              /* 目标偏移 */
    .word   0x1000              /* 目标段（物理地址 = 0x10000）*/
    .long   1                   /* LBA 起始扇区低 32 位（扇区 1，MBR 之后）*/
    .long   0                   /* LBA 起始扇区高 32 位 */

/* 引导驱动器号暂存 */
boot_drive_save:
    .byte   0x80
