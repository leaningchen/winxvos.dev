/*===========================================================================
 * linker/kernel.ld — WinixOS 内核链接脚本（高地址版本）
 *
 * 内核被链接到虚拟地址 KERNBASE + 0x100000，其中：
 *   KERNBASE = 0xFFFF800000000000（x86-64 高半规范地址）
 *   0x100000  = 1MB（物理加载地址，由 bootloader 将内核加载到此处）
 *
 * 因此：
 *   链接地址（VMA）= 0xFFFF800000100000  （高地址虚拟地址）
 *   加载地址（LMA）= 0x100000            （物理地址）
 *
 * entry64.S 负责在跳转到高地址之前建立临时页表，使得
 * 虚拟地址 KERNBASE+PA == 物理地址 PA 的映射成立。
 *===========================================================================*/

OUTPUT_FORMAT("elf64-x86-64")
ENTRY(kernel_entry)

/* 内核虚拟基址（与 memlayout.h 中的 KERNBASE 一致）*/
KERNBASE = 0xFFFF800000000000;

SECTIONS {
    /* 链接地址从 KERNBASE + 1MB 开始 */
    . = KERNBASE + 0x100000;

    /* .text.entry 必须最先，确保 kernel_entry 在镜像起始处
     * entry64.S 的前半段（物理地址时期）也需要在此段中 */
    .text : ALIGN(0x1000) {
        *(.text.entry)
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(0x1000) {
        /* AP 蹦床二进制嵌入内核镜像 */
        __trampoline_start = .;
        *(.trampoline)
        __trampoline_end = .;

        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(0x1000) {
        /* 静态临时页表（由 entry64.S 使用，需要 4KB 对齐）*/
        *(.data.pagetable)
        *(.data)
        *(.data.*)
    }

    . = ALIGN(0x1000);
    __bss_start = .;
    .bss : {
        *(.bss)
        *(.bss.*)
        *(.lbss)
        *(.lbss.*)
        *(COMMON)
    }
    __bss_end = .;

    /* initcode 二进制（嵌入为只读数据，userinit() 将其映射到第一个进程）*/
    .initcode : ALIGN(0x1000) {
        *(.initcode)
    }

    . = ALIGN(0x1000);
    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}

