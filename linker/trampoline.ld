OUTPUT_FORMAT("elf32-i386")
ENTRY(ap_start)

SECTIONS {
    . = 0x8000;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.bss)
        *(COMMON)
    }
}
