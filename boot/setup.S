/* boot/stage2.S - Stage2 Boot Loader
 * Load address: 0x10000
 *
 * Key constraints:
 *   - All R_386_16 relocations in .code16 must ref symbols < 0x10000
 *   - GDT written at runtime to physical 0x9000; GDT ptr at 0x9028
 *   - Temp vars at 0x4000-0x400F (DS=0 absolute addressing)
 *   - 32-bit far jump hand-encoded 0x66 0xEA to avoid R_386_16 overflow

 * boot/stage2.S — 第二阶段加载器
 * 链接地址: 0x10000（实模式段 0x1000:0x0000）
 *
 * 执行流程:
 *   [.code16] 实模式（DS=0x1000，段内偏移访问所有变量）:
 *     - 保存引导驱动器号（DL）→ s2_drive
 *     - E820 内存探测 → 0x6000
 *     - VBE 图形模式设置（1680x1050 → 1440x900 → 800x600）
 *     - ACPI RSDP 探测
 *     - 填写 BootInfo（0x5000）magic/version
 *     - 构建 GDT，进入 32 位保护模式
 *   [.code32] 32 位保护模式:
 *     - CPUID 长模式检查
 *     - PIO ATA（LBA28）加载内核 → 0x100000
 *     - 构建 4 级页表（Identity Map 前 4GB，2MB 大页）
 *     - 进入 64 位长模式，远跳 kernel_entry@0x100000
 *
 * KERNEL_LBA / KERNEL_SECTORS 由 Makefile 通过 --defsym 传入
 */

#ifndef KERNEL_LBA
#define KERNEL_LBA 65
#endif
#ifndef KERNEL_SECTORS
#define KERNEL_SECTORS 256
#endif

.equ TMP_DRIVE,   0x500   /* byte:  boot drive */
.equ TMP_M1680,   0x502   /* word:  VBE mode 1680x1050 */
.equ TMP_M1440,   0x504   /* word:  VBE mode 1440x900  */
.equ TMP_M800,    0x506   /* word:  VBE mode 800x600   */
.equ TMP_E820CNT, 0x508   /* dword: E820 entry count   */
.equ BOOT_INFO,  0x5000
.equ E820_BUF,   0x6000
.equ VBE_INFO,   0x7000
.equ VBE_MODE,   0x7200
.equ GDT_BASE,   0x9000
.equ SEL_CODE32, 0x08
.equ SEL_DATA32, 0x10
.equ SEL_CODE64, 0x18
.equ SEL_DATA64, 0x20
.equ PML4_BASE,  0x70000
.equ PDPT_BASE,  0x71000
.equ PD0_BASE,   0x72000
.equ PD1_BASE,   0x73000
.equ PD2_BASE,   0x74000
.equ PD3_BASE,   0x75000
.equ BI_MAGIC,      0x00
.equ BI_E820_COUNT, 0x20
.equ BI_E820_ADDR,  0x24
.equ BI_FB_ADDR,    0x08
.equ BI_FB_WIDTH,   0x14
.equ BI_FB_HEIGHT,  0x18
.equ BI_FB_PITCH,   0x10
.equ BI_FB_BPP,     0x1C
.equ BI_ACPI_RSDP,  0x30
.equ TMP_RSDP_LO,   0x50C   /* dword: RSDP 物理地址低32位 */

    .code16
    .section .text
    .global _start

_start:
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x3FFE, %sp        /* 栈在 0x3FFE，远离 TMP 区 0x4FF0 */
    movb    %dl, (TMP_DRIVE)
    movw    $0, (TMP_M1680)
    movw    $0, (TMP_M1440)
    movw    $0, (TMP_M800)
    call    init_gdt

e820_detect:
    xorw    %ax, %ax
    movw    %ax, %es
    movw    $E820_BUF, %di
    xorl    %ebx, %ebx
    xorl    %ebp, %ebp
.e820_loop:
    movl    $0xE820, %eax
    movl    $24, %ecx
    movl    $0x534D4150, %edx
    int     $0x15
    jc      .e820_done
    cmpl    $0x534D4150, %eax
    jne     .e820_done
    testl   %ecx, %ecx
    jz      .e820_next
    cmpl    $20, %ecx
    jbe     .e820_store
    testb   $1, 20(%di)
    jz      .e820_next
.e820_store:
    addw    $24, %di
    incl    %ebp
    cmpl    $128, %ebp
    jae     .e820_done
.e820_next:
    testl   %ebx, %ebx
    jz      .e820_done
    jmp     .e820_loop
.e820_done:
    movl    %ebp, (TMP_E820CNT)   /* save count; boot_info not cleared yet */

/* -----------------------------------------------------------------------
 * rsdp_search — 搜索 ACPI RSDP 签名 "RSD PTR "
 * 搜索范围：
 *   1. EBDA: 从 [0x40E]*16 开始，搜索前 1KB
 *   2. BIOS ROM: 0xE0000 - 0xFFFFF
 * RSDP 始终对齐到 16 字节边界
 * 找到后将物理地址存入 TMP_RSDP_LO；未找到则存 0
 * -----------------------------------------------------------------------*/
rsdp_search:
    movl    $0, (TMP_RSDP_LO)

    /* --- 搜索 EBDA --- */
    movw    (0x40E), %ax        /* EBDA 段地址 */
    testw   %ax, %ax
    jz      .rsdp_bios          /* 无 EBDA，直接搜 BIOS ROM */
    movw    %ax, %fs
    xorw    %si, %si            /* 段内偏移 0 */
    movw    $64, %cx            /* 1KB / 16 = 64 次 */
.rsdp_ebda_loop:
    /* 比较 "RSD PTR " (8 bytes): fs:si */
    movl    %fs:(%si), %eax
    cmpl    $0x20445352, %eax   /* "RSD " little-endian */
    jne     .rsdp_ebda_next
    movl    %fs:4(%si), %eax
    cmpl    $0x20525450, %eax   /* "PTR " little-endian */
    jne     .rsdp_ebda_next
    /* 找到：计算物理地址 = seg*16 + offset */
    movw    %fs, %ax
    movzwl  %ax, %eax
    shll    $4, %eax
    movzwl  %si, %edx
    addl    %edx, %eax
    movl    %eax, (TMP_RSDP_LO)
    jmp     .rsdp_done
.rsdp_ebda_next:
    addw    $16, %si
    loop    .rsdp_ebda_loop

    /* --- 搜索 BIOS ROM 0xE0000 - 0xFFFFF --- */
.rsdp_bios:
    movw    $0xE000, %ax
    movw    %ax, %fs            /* 段 0xE000 → 物理 0xE0000 */
    xorw    %si, %si
    movw    $0x1000, %cx
.rsdp_bios_loop:
    movl    %fs:(%si), %eax
    cmpl    $0x20445352, %eax
    jne     .rsdp_bios_next
    movl    %fs:4(%si), %eax
    cmpl    $0x20525450, %eax
    jne     .rsdp_bios_next
    movw    %fs, %ax
    movzwl  %ax, %eax
    shll    $4, %eax
    movzwl  %si, %edx
    addl    %edx, %eax
    movl    %eax, (TMP_RSDP_LO)
    jmp     .rsdp_done
.rsdp_bios_next:
    addw    $16, %si
    jnz     .rsdp_bios_loop     /* si 回绕到 0 说明段内搜完 */
    /* 进入下一个段 0xF000 */
    movw    $0xF000, %ax
    movw    %ax, %fs
    movw    $0x1000, %cx
.rsdp_f000_loop:
    movl    %fs:(%si), %eax
    cmpl    $0x20445352, %eax
    jne     .rsdp_f000_next
    movl    %fs:4(%si), %eax
    cmpl    $0x20525450, %eax
    jne     .rsdp_f000_next
    movw    %fs, %ax
    movzwl  %ax, %eax
    shll    $4, %eax
    movzwl  %si, %edx
    addl    %edx, %eax
    movl    %eax, (TMP_RSDP_LO)
    jmp     .rsdp_done
.rsdp_f000_next:
    addw    $16, %si
    loop    .rsdp_f000_loop
.rsdp_done:

vbe_detect:
    xorw    %ax, %ax
    movw    %ax, %es
    movw    $VBE_INFO, %di
    movb    $0x56, (VBE_INFO + 0)
    movb    $0x42, (VBE_INFO + 1)
    movb    $0x45, (VBE_INFO + 2)
    movb    $0x32, (VBE_INFO + 3)
    movw    $0x4F00, %ax
    int     $0x10
    cmpw    $0x004F, %ax
    jne     .vbe_skip_scan
    movw    (VBE_INFO + 0x10), %ax
    movw    %ax, %fs
    movw    (VBE_INFO + 0x0E), %si
.vbe_mode_scan:
    movw    %fs:(%si), %cx
    cmpw    $0xFFFF, %cx
    je      .vbe_scan_done
    addw    $2, %si
    xorw    %ax, %ax
    movw    %ax, %es
    movw    $VBE_MODE, %di
    movw    $0x4F01, %ax
    int     $0x10
    cmpw    $0x004F, %ax
    jne     .vbe_mode_scan
    movw    (VBE_MODE + 0x00), %ax
    testw   $0x0080, %ax
    jz      .vbe_mode_scan
    testw   $0x0010, %ax
    jz      .vbe_mode_scan
    testw   $0x0001, %ax
    jz      .vbe_mode_scan
    movb    (VBE_MODE + 0x19), %al
    cmpb    $32, %al
    jne     .vbe_mode_scan
    movb    (VBE_MODE + 0x1B), %al
    cmpb    $6, %al
    jne     .vbe_mode_scan
    movw    (VBE_MODE + 0x12), %ax
    movw    (VBE_MODE + 0x14), %bx
    cmpw    $1680, %ax
    jne     .chk1440
    cmpw    $1050, %bx
    jne     .chk1440
    movw    %cx, (TMP_M1680)
    jmp     .vbe_mode_scan
.chk1440:
    cmpw    $1440, %ax
    jne     .chk800
    cmpw    $900, %bx
    jne     .chk800
    movw    %cx, (TMP_M1440)
    jmp     .vbe_mode_scan
.chk800:
    cmpw    $800, %ax
    jne     .vbe_mode_scan
    cmpw    $600, %bx
    jne     .vbe_mode_scan
    movw    %cx, (TMP_M800)
    jmp     .vbe_mode_scan
.vbe_scan_done:
    movw    (TMP_M1680), %cx
    testw   %cx, %cx
    jnz     .vbe_set_mode
    movw    (TMP_M1440), %cx
    testw   %cx, %cx
    jnz     .vbe_set_mode
    movw    (TMP_M800), %cx
    testw   %cx, %cx
    jz      .vbe_skip_scan
.vbe_set_mode:
    xorw    %ax, %ax
    movw    %ax, %es
    movw    $VBE_MODE, %di
    movw    $0x4F01, %ax
    int     $0x10
    orw     $0x4000, %cx
    movw    $0x4F02, %ax
    movw    %cx, %bx
    int     $0x10
.vbe_skip_scan:

init_boot_info:
    xorw    %ax, %ax
    movw    %ax, %es
    movw    $BOOT_INFO, %di
    movw    $32, %cx            /* 清 64 字节 (0x40)，覆盖所有字段到 0x3F */
    xorw    %ax, %ax
    rep stosw
    movl    $0xB007B007, %eax
    movl    %eax, (BOOT_INFO + BI_MAGIC)
    movl    (TMP_E820CNT), %eax
    movl    %eax, (BOOT_INFO + BI_E820_COUNT)
    movl    $E820_BUF, (BOOT_INFO + BI_E820_ADDR)
    movl    (VBE_MODE + 0x28), %eax
    movl    %eax, (BOOT_INFO + BI_FB_ADDR)
    movl    $0, (BOOT_INFO + BI_FB_ADDR + 4)
    movzwl  (VBE_MODE + 0x12), %eax
    movl    %eax, (BOOT_INFO + BI_FB_WIDTH)
    movzwl  (VBE_MODE + 0x14), %eax
    movl    %eax, (BOOT_INFO + BI_FB_HEIGHT)
    movzwl  (VBE_MODE + 0x10), %eax
    movl    %eax, (BOOT_INFO + BI_FB_PITCH)
    movb    (VBE_MODE + 0x19), %al
    movb    %al, (BOOT_INFO + BI_FB_BPP)
    /* ACPI RSDP 物理地址（64位，高32位为0）*/
    movl    (TMP_RSDP_LO), %eax
    movl    %eax, (BOOT_INFO + BI_ACPI_RSDP)
    movl    $0,   (BOOT_INFO + BI_ACPI_RSDP + 4)

enter_pm:
    cli
    lgdt    (GDT_BASE + 0x28)
    movl    %cr0, %eax
    orl     $0x1, %eax
    movl    %eax, %cr0
    /* hand-encoded 32-bit far jump: operand-size prefix + LJMPF */
    .byte   0x66, 0xEA
    .long   pm32_entry
    .word   SEL_CODE32

init_gdt:
    pushw   %di
    movw    $GDT_BASE, %di
    /* NULL */
    movl    $0, (%di);           movl $0,          4(%di); addw $8, %di
    /* CODE32 */
    movl    $0x0000FFFF, (%di);  movl $0x00CF9A00, 4(%di); addw $8, %di
    /* DATA32 */
    movl    $0x0000FFFF, (%di);  movl $0x00CF9200, 4(%di); addw $8, %di
    /* CODE64 (L=1) */
    movl    $0x0000FFFF, (%di);  movl $0x00AF9A00, 4(%di); addw $8, %di
    /* DATA64 */
    movl    $0x0000FFFF, (%di);  movl $0x00AF9200, 4(%di); addw $8, %di
    /* GDT ptr at 0x9028 */
    movw    $(5*8-1), (%di)
    movl    $GDT_BASE, 2(%di)
    popw    %di
    ret

    .code32
pm32_entry:
    movw    $SEL_DATA32, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    $0x9FFFC, %esp

load_kernel:
    movl    $KERNEL_LBA, %esi
    movl    $KERNEL_SECTORS, %ecx
    movl    $0x100000, %ebx
.lk_loop:
    testl   %ecx, %ecx
    jz      .lk_done
.lk_wait1:
    movw    $0x1F7, %dx
    inb     %dx, %al
    testb   $0x80, %al
    jnz     .lk_wait1
    testb   $0x40, %al
    jz      .lk_wait1
    movl    $1, %eax
    movw    $0x1F2, %dx
    outb    %al, %dx
    movl    %esi, %eax
    movw    $0x1F3, %dx
    outb    %al, %dx
    movb    %ah, %al
    movw    $0x1F4, %dx
    outb    %al, %dx
    shrl    $16, %eax
    movw    $0x1F5, %dx
    outb    %al, %dx
    movl    %esi, %eax
    shrl    $24, %eax
    orb     $0xE0, %al
    movw    $0x1F6, %dx
    outb    %al, %dx
    movb    $0x20, %al
    movw    $0x1F7, %dx
    outb    %al, %dx
.lk_wait2:
    movw    $0x1F7, %dx
    inb     %dx, %al
    testb   $0x80, %al
    jnz     .lk_wait2
    testb   $0x08, %al
    jz      .lk_wait2
    pushl   %ecx
    movw    $0x1F0, %dx
    movl    $256, %ecx
    movl    %ebx, %edi
    rep insw
    popl    %ecx
    addl    $512, %ebx
    incl    %esi
    decl    %ecx
    jmp     .lk_loop
.lk_done:

setup_paging:
    movl    $PML4_BASE, %edi
    movl    $(6*4096/4), %ecx
    xorl    %eax, %eax
    rep stosl
    movl    $(PDPT_BASE|0x3), (PML4_BASE)
    movl    $0, (PML4_BASE+4)
    movl    $(PD0_BASE|0x3), (PDPT_BASE+0)
    movl    $0, (PDPT_BASE+4)
    movl    $(PD1_BASE|0x3), (PDPT_BASE+8)
    movl    $0, (PDPT_BASE+12)
    movl    $(PD2_BASE|0x3), (PDPT_BASE+16)
    movl    $0, (PDPT_BASE+20)
    movl    $(PD3_BASE|0x3), (PDPT_BASE+24)
    movl    $0, (PDPT_BASE+28)
    movl    $PD0_BASE, %edi
    movl    $0x00000183, %eax
    movl    $512, %ecx
.pd0: movl %eax,(%edi); movl $0,4(%edi); addl $0x200000,%eax; addl $8,%edi; loop .pd0
    movl    $PD1_BASE, %edi
    movl    $0x40000183, %eax
    movl    $512, %ecx
.pd1: movl %eax,(%edi); movl $0,4(%edi); addl $0x200000,%eax; addl $8,%edi; loop .pd1
    movl    $PD2_BASE, %edi
    movl    $0x80000183, %eax
    movl    $512, %ecx
.pd2: movl %eax,(%edi); movl $0,4(%edi); addl $0x200000,%eax; addl $8,%edi; loop .pd2
    movl    $PD3_BASE, %edi
    movl    $0xC0000183, %eax
    movl    $512, %ecx
.pd3: movl %eax,(%edi); movl $0,4(%edi); addl $0x200000,%eax; addl $8,%edi; loop .pd3

enter_lm:
    lgdt    (GDT_BASE + 0x28)
    movl    %cr4, %eax
    orl     $(1<<5)|(1<<4), %eax
    movl    %eax, %cr4
    movl    $PML4_BASE, %eax
    movl    %eax, %cr3
    movl    $0xC0000080, %ecx
    rdmsr
    orl     $(1<<8), %eax
    wrmsr
    movl    %cr0, %eax
    orl     $(1<<31), %eax
    movl    %eax, %cr0
    ljmp    $SEL_CODE64, $lm64_entry

    .code64
lm64_entry:
    movw    $SEL_DATA64, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movq    $0x9FFF0, %rsp
    movq    $BOOT_INFO, %rdi
    movq    $0x100000, %rax
    jmpq    *%rax
.lm_halt:
    cli
    hlt
    jmp .lm_halt
