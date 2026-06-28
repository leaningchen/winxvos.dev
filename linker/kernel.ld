/* ============================================================================
 * kernel.ld — 64位高半核内核链接器脚本
 * 将 VMA 映射到高半核，同时通过 AT() 控制 LMA，确保 objcopy 生成正确二进制。
 * ============================================================================ */

OUTPUT_FORMAT("elf64-x86-64")
ENTRY(kernel_entry)

HHDM_OFFSET = 0xFFFF800000000000;
KERNEL_PHYS_BASE = 0x100000;

SECTIONS {
    . = HHDM_OFFSET + KERNEL_PHYS_BASE;

    .text ALIGN(0x1000) : AT(ADDR(.text) - HHDM_OFFSET) {
        *(.text.entry)
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(0x1000) : AT(ADDR(.rodata) - HHDM_OFFSET) {
        __trampoline_start = .;
        *(.trampoline)
        __trampoline_end = .;

        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(0x1000) : AT(ADDR(.data) - HHDM_OFFSET) {
        *(.data)
        *(.data.*)
    }

    . = ALIGN(0x1000);
    __bss_start = .;
    .bss ALIGN(0x1000) : AT(ADDR(.bss) - HHDM_OFFSET) {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    __bss_end = .;

    . = ALIGN(0x1000);
    __kernel_end = .;

    __kernel_phys_start = KERNEL_PHYS_BASE;
    __kernel_phys_end = __kernel_end - HHDM_OFFSET;
    __trampoline_phys_start = __trampoline_start - HHDM_OFFSET;
    __trampoline_phys_end   = __trampoline_end - HHDM_OFFSET;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.note.gnu.build-id*)
    }
}