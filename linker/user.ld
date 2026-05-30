/*===========================================================================
 * linker/user.ld — 用户态程序链接脚本
 *
 * 用户程序加载到虚拟地址 0x400000（4MB），避免与 0 地址冲突。
 * exec.c 会将 PT_LOAD 段映射到指定 vaddr，并在 vaddr+memsz 之后
 * 分配用户栈（2 页）。
 *===========================================================================*/

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(main)

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text .text.*)
    }

    . = ALIGN(4096);
    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4096);
    .data : {
        *(.data .data.*)
    }

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }

    . = ALIGN(4096);
}
