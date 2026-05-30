OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

SECTIONS {
    . = 0x7C00;

    .text : {
        *(.text)
        *(.text.*)
    }

    /* 填充到 510 字节处，追加启动签名 */
    . = 0x7C00 + 510;
    .sig : {
        SHORT(0xAA55)
    }
}
