OUTPUT_FORMAT("elf64-x86-64")
ENTRY(kernel_entry)

SECTIONS {
    . = 0x100000;

    /* .text.entry 必须最先，确保 kernel_entry 在镜像起始处 */
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
        *(.data)
        *(.data.*)
    }

    . = ALIGN(0x1000);
    __bss_start = .;
    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    __bss_end = .;

    . = ALIGN(0x1000);
    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
