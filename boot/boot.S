/*
 * boot/stage1.S — 增强型 MBR 引导扇区
 */

#ifndef STAGE2_SECTORS
#define STAGE2_SECTORS 64
#endif

    .code16
    .section .text
    .global _start

_start:
    /* 1. MBR 起始跳转（避开可能存在的 BPB 区域） */
    jmp entrance
    nop
    .space 60, 0        /* 为 BPB 预留空间，防止 BIOS 修改代码 */

entrance:
    /* 2. 规范化 CS:IP 和段寄存器 */
    ljmp    $0x0000, $entry
entry:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7C00, %sp        /* 栈基址设为 0x7C00 */
    cld

    /* 保存 BIOS 传入的驱动器号 */
    movb    %dl, boot_drive_save

    /* 3. 开启 A20 地址线 */
    /* 方式 A: BIOS 中断方式 (最安全) */
    movw    $0x2401, %ax
    int     $0x15
    jnc     a20_done

    /* 方式 B: Fast A20 (端口 0x92) */
    inb     $0x92, %al
    orb     $0x02, %al
    andb    $0xFE, %al
    outb    %al, $0x92

a20_done:
    /* 4. 检查 BIOS 是否支持 INT 13h 扩展 (LBA) */
    movb    boot_drive_save, %dl
    movw    $0x55AA, %bx
    movb    $0x41, %ah
    int     $0x13
    jc      lba_not_supported
    cmpw    $0xAA55, %bx
    jne     lba_not_supported
    testb   $0x01, %cl          /* bit 0: 必须支持扩展读 AH=42h */
    jz      lba_not_supported

    /* 5. 使用 LBA 模式加载 Stage2 */
    movw    $dap, %si           /* DS:SI 指向 DAP */
    movb    boot_drive_save, %dl
    movb    $0x42, %ah
    int     $0x13
    jc      read_error

    /* 6. 跳转到 Stage2 (0x1000:0000) */
    movb    $'S', %al           /* 打印 'S' 表示 Success */
    call    print_char
    
    ljmp    $0x1000, $0x0000

/* --- 错误处理 --- */

lba_not_supported:
    movb    $'L', %al           /* 'L' = LBA Error */
    jmp     die

read_error:
    movb    $'R', %al           /* 'R' = Read Error */
    jmp     die

die:
    call    print_char
halt_loop:
    cli
    hlt
    jmp     halt_loop

/* --- 辅助函数 --- */

print_char:
    movb    $0x0E, %ah
    movw    $0x0007, %bx
    int     $0x10
    ret

/* --- 数据区 --- */

    .balign 16
dap:
    .byte   0x10                /* DAP 结构体大小 */
    .byte   0x00                /* 保留 */
    .word   STAGE2_SECTORS      /* 读取扇区数量 */
    .word   0x0000              /* 目标偏移 */
    .word   0x1000              /* 目标段地址 */
    .quad   1                   /* 起始 LBA (扇区 1 是 MBR 之后第一个扇区) */

boot_drive_save:
    .byte   0x00

/* 
 * --------------------------------------------------------
 * MBR 分区表 (从偏移 446 开始)
 * --------------------------------------------------------
 */
    .org 446

/* 分区 1: 256MB ext4 分区 */
partition1:
    .byte   0x80                /* 0: 引导标志 (0x80 = 活动) */
    .byte   0x20, 0x21, 0x00    /* 1-3: 起始 CHS (现代系统多忽略，对应 LBA 2048 常用 0,32,33) */
    .byte   0x83                /* 4: 分区类型 (0x83 = Linux) */
    .byte   0xFE, 0xFF, 0xFF    /* 5-7: 结束 CHS (通常填最大值) */
    .long   2048                /* 8-11: 起始 LBA (2048) */
    .long   524288              /* 12-15: 总扇区数 (256MB / 512B = 524288) */

/* 分区 2, 3, 4: 填充为 0 */
    .fill   16, 1, 0
    .fill   16, 1, 0
    .fill   16, 1, 0