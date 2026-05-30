/*===========================================================================
 * linker/initcode.ld — initcode.S 链接脚本
 *
 * 链接为从虚拟地址 0 开始的平坦二进制，userinit() 将其加载到
 * 第一个进程的地址空间第 0 页。
 *===========================================================================*/

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS {
    . = 0x0000;

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data)
        *(.bss)
    }

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.eh_frame*)
    }
}
