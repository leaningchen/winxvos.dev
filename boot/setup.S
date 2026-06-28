/* boot/stage2.S - Stage2 Boot Loader (Production Ready with Higher Half)
 * Load address: 0x10000
 *
 * 核心架构特性:
 *   - 高半核映射 (Higher Half Direct Map): 物理 0-4GB 映射到 0xFFFF800000000000+
 *   - AVX/SSE/SSE2 硬件级开启与 XSAVE 状态配置
 *   - 4级页表构建 (2MB 大页, PML4[0] & PML4[256])
 *   - SMEP 安全特性开启
 *   - 完整的硬件信息收集 (E820, VBE, RSDP, SMBIOS)
 *   - 底层硬件安全初始化 (A20, PIC, 8042, Serial)
 */

#ifndef KERNEL_LBA
#define KERNEL_LBA 65
#endif
#ifndef KERNEL_SECTORS
#define KERNEL_SECTORS 256
#endif

/* 高半核虚拟地址基址 (Higher Half Direct Map Base) - 对应 PML4[256] */
.equ HHDM_OFFSET,    0xFFFF800000000000

/* 临时变量区 (DS=0 绝对寻址) */
.equ TMP_DRIVE,       0x500   
.equ TMP_M1680,       0x502   
.equ TMP_M1440,       0x504   
.equ TMP_M800,        0x506   
.equ TMP_E820CNT,     0x508   
.equ TMP_RSDP_LO,     0x50C   
.equ TMP_MEM_LO,      0x510   
.equ TMP_MEM_HI,      0x514   
.equ TMP_SMBIOS,      0x518   /* dword: SMBIOS 物理地址 */

/* 内存布局常量 (物理地址) */
.equ BOOT_INFO,       0x5000
.equ E820_BUF,        0x6000
.equ VBE_INFO,        0x7000
.equ VBE_MODE,        0x7200
.equ GDT_BASE,        0x9000

/* 段选择子 */
.equ SEL_CODE32,      0x08
.equ SEL_DATA32,      0x10
.equ SEL_CODE64,      0x18
.equ SEL_DATA64,      0x20

/* 页表基址 (物理地址) */
.equ PML4_BASE,       0x70000
.equ PDPT_BASE,       0x71000
.equ PD0_BASE,        0x72000
.equ PD1_BASE,        0x73000
.equ PD2_BASE,        0x74000
.equ PD3_BASE,        0x75000

/* zenith_boot_info 结构体偏移量 */
.equ BI_MAGIC,        0x00
.equ BI_VERSION,      0x04
.equ BI_FB_ADDR,      0x08
.equ BI_FB_PITCH,     0x10
.equ BI_FB_WIDTH,     0x14
.equ BI_FB_HEIGHT,    0x18
.equ BI_FB_BPP,       0x1C
.equ BI_E820_COUNT,   0x20
.equ BI_E820_ADDR,    0x24
.equ BI_MEM_TOTAL,    0x28
.equ BI_ACPI_RSDP,    0x30
.equ BI_BOOT_DRIVE,   0x38
.equ BI_CPU_COUNT,    0x3C
.equ BI_SMBIOS_ADDR,  0x40    
/* 0x44 - 0x49 用于 VBE 颜色掩码 */

    .code16
    .section .text
    .global _start

_start:
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x3FFE, %sp        
    
    movb    %dl, (TMP_DRIVE)
    movw    $0, (TMP_M1680)
    movw    $0, (TMP_M1440)
    movw    $0, (TMP_M800)
    movl    $0, (TMP_MEM_LO)
    movl    $0, (TMP_MEM_HI)
    movl    $0, (TMP_SMBIOS)
    
    call    init_serial         /* 初始化 COM1 串口，便于早期调试 */
    call    enable_a20          
    call    flush_8042          /* 清空键盘控制器缓冲区 */
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
    jb      .e820_next          
    cmpl    $24, %ecx
    jb      .e820_store         
    testb   $1, 20(%di)         
    jz      .e820_next          
.e820_store:
    cmpl    $1, 16(%di)
    jne     .e820_skip_mem
    movl    8(%di), %eax        
    movl    12(%di), %edx       
    addl    %eax, (TMP_MEM_LO)
    adcl    %edx, (TMP_MEM_HI)
.e820_skip_mem:
    addw    $24, %di            
    incl    %ebp
    cmpl    $128, %ebp
    jae     .e820_done
.e820_next:
    testl   %ebx, %ebx
    jz      .e820_done
    jmp     .e820_loop
.e820_done:
    movl    %ebp, (TMP_E820CNT)

rsdp_search:
    movl    $0, (TMP_RSDP_LO)
    movw    (0x40E), %ax
    testw   %ax, %ax
    jz      .rsdp_bios
    movw    %ax, %fs
    xorw    %si, %si
    movw    $64, %cx
.rsdp_ebda_loop:
    movl    %fs:(%si), %eax
    cmpl    $0x20445352, %eax
    jne     .rsdp_ebda_next
    movl    %fs:4(%si), %eax
    cmpl    $0x20525450, %eax
    jne     .rsdp_ebda_next
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
.rsdp_bios:
    movw    $0xE000, %ax
    movw    %ax, %fs
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
    jnz     .rsdp_bios_loop
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

smbios_search:
    movw    $0xF000, %ax
    movw    %ax, %es
    xorw    %di, %di
    movw    $0x1000, %cx
.smbios_loop:
    movl    %es:(%di), %eax
    cmpl    $0x5F4D535F, %eax   /* "_SM_" */
    je      .smbios_found
    cmpl    $0x5F494D44, %eax   /* "_DMI_" */
    je      .smbios_found
    addw    $16, %di            
    loop    .smbios_loop
    movl    $0, (TMP_SMBIOS)
    jmp     .smbios_done
.smbios_found:
    movzwl  %di, %eax
    addl    $0xF0000, %eax
    movl    %eax, (TMP_SMBIOS)
.smbios_done:

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
    call    disable_pic         

    xorw    %ax, %ax
    movw    %ax, %es
    movw    $BOOT_INFO, %di
    movw    $32, %cx            /* 清空 64 字节 */
    xorw    %ax, %ax
    rep stosw
    
    movl    $0xB007B007, %eax
    movl    %eax, (BOOT_INFO + BI_MAGIC)
    movl    $1, %eax
    movl    %eax, (BOOT_INFO + BI_VERSION)
    
    movl    (VBE_MODE + 0x28), %eax
    movl    %eax, (BOOT_INFO + BI_FB_ADDR)
    movl    $0, (BOOT_INFO + BI_FB_ADDR + 4)
    
    movzwl  (VBE_MODE + 0x10), %eax
    movl    %eax, (BOOT_INFO + BI_FB_PITCH)
    movzwl  (VBE_MODE + 0x12), %eax
    movl    %eax, (BOOT_INFO + BI_FB_WIDTH)
    movzwl  (VBE_MODE + 0x14), %eax
    movl    %eax, (BOOT_INFO + BI_FB_HEIGHT)
    movb    (VBE_MODE + 0x19), %al
    movb    %al, (BOOT_INFO + BI_FB_BPP)
    
    /* 收集 VBE 颜色掩码 (VBE 3.0 Direct Color 必需) */
    movb    (VBE_MODE + 0x24), %al
    movb    %al, (BOOT_INFO + 0x44) /* Red Mask Size */
    movb    (VBE_MODE + 0x25), %al
    movb    %al, (BOOT_INFO + 0x45) /* Red Field Pos */
    movb    (VBE_MODE + 0x26), %al
    movb    %al, (BOOT_INFO + 0x46) /* Green Mask Size */
    movb    (VBE_MODE + 0x27), %al
    movb    %al, (BOOT_INFO + 0x47) /* Green Field Pos */
    movb    (VBE_MODE + 0x28), %al
    movb    %al, (BOOT_INFO + 0x48) /* Blue Mask Size */
    movb    (VBE_MODE + 0x29), %al
    movb    %al, (BOOT_INFO + 0x49) /* Blue Field Pos */
    
    movl    (TMP_E820CNT), %eax
    movl    %eax, (BOOT_INFO + BI_E820_COUNT)
    movl    $E820_BUF, (BOOT_INFO + BI_E820_ADDR)
    
    movl    (TMP_MEM_LO), %eax
    movl    %eax, (BOOT_INFO + BI_MEM_TOTAL)
    movl    (TMP_MEM_HI), %eax
    movl    %eax, (BOOT_INFO + BI_MEM_TOTAL + 4)
    
    movl    (TMP_RSDP_LO), %eax
    movl    %eax, (BOOT_INFO + BI_ACPI_RSDP)
    movl    $0,   (BOOT_INFO + BI_ACPI_RSDP + 4)
    
    movzbl  (TMP_DRIVE), %eax
    movl    %eax, (BOOT_INFO + BI_BOOT_DRIVE)
    
    movl    (TMP_SMBIOS), %eax
    movl    %eax, (BOOT_INFO + BI_SMBIOS_ADDR)

enter_pm:
    cli
    lgdt    (GDT_BASE + 0x28)
    movl    %cr0, %eax
    orl     $0x1, %eax
    movl    %eax, %cr0
    /* 使用 ljmpl 强制生成 32 位偏移的远跳转 */
    ljmpl   $SEL_CODE32, $pm32_entry 

/* 辅助子程序 */
enable_a20:
    movw    $0x2403, %ax
    int     $0x15
    jc      .a20_fallback
    testb   $0x02, %al
    jnz     .a20_done
    movw    $0x2401, %ax
    int     $0x15
    jnc     .a20_done
.a20_fallback:
    inb     $0x92, %al
    testb   $0x02, %al
    jnz     .a20_done
    orb     $0x02, %al
    outb    %al, $0x92
.a20_done:
    ret

flush_8042:
.flush_loop:
    inb     $0x64, %al
    testb   $0x01, %al
    jz      .flush_done
    inb     $0x60, %al
    jmp     .flush_loop
.flush_done:
    ret

init_serial:
    movw    $0x3F8, %dx
    movb    $0x00, %al
    movw    $0x3F9, %dx
    outb    %al, %dx
    movb    $0x80, %al
    movw    $0x3FB, %dx
    outb    %al, %dx
    movb    $0x01, %al
    movw    $0x3F8, %dx
    outb    %al, %dx
    movb    $0x00, %al
    movw    $0x3F9, %dx
    outb    %al, %dx
    movb    $0x03, %al
    movw    $0x3FB, %dx
    outb    %al, %dx
    movb    $0xC7, %al
    movw    $0x3FA, %dx
    outb    %al, %dx
    movb    $0x0B, %al
    movw    $0x3FC, %dx
    outb    %al, %dx
    ret

disable_pic:
    movb    $0xFF, %al
    outb    %al, $0x21
    outb    %al, $0xA1
    ret

init_gdt:
    pushw   %di
    movw    $GDT_BASE, %di
    movl    $0, (%di);           movl $0,          4(%di); addw $8, %di
    movl    $0x0000FFFF, (%di);  movl $0x00CF9A00, 4(%di); addw $8, %di
    movl    $0x0000FFFF, (%di);  movl $0x00CF9200, 4(%di); addw $8, %di
    movl    $0x0000FFFF, (%di);  movl $0x00AF9A00, 4(%di); addw $8, %di
    movl    $0x0000FFFF, (%di);  movl $0x00AF9200, 4(%di); addw $8, %di
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

    /* 1. 开启 FPU / SSE / SSE2 */
    movl    %cr0, %eax
    andl    $~(1 << 2), %eax    
    orl     $(1 << 1), %eax     
    movl    %eax, %cr0
    movl    %cr4, %eax
    orl     $(1 << 9), %eax     
    orl     $(1 << 10), %eax    
    movl    %eax, %cr4

    /* 2. 开启 AVX (如果支持) */
    movl    $1, %eax
    cpuid
    testl   $(1 << 27), %ecx    
    jz      .skip_avx
    testl   $(1 << 28), %ecx    
    jz      .skip_avx
    movl    %cr4, %eax
    orl     $(1 << 18), %eax    /* CR4.OSXSAVE */
    movl    %eax, %cr4
    xorl    %ecx, %ecx          
    xgetbv                      
    orl     $0x7, %eax          /* x87, SSE, AVX */
    xsetbv                      
.skip_avx:

    /* 3. 清理 MSR 和 EFLAGS */
    pushl   $0x00000002         
    popfl
    cld                         
    xorl    %eax, %eax
    xorl    %edx, %edx
    movl    $0x1D9, %ecx        
    wrmsr

    /* 4. 校验长模式 & 获取基础 SMP 信息 & 安全特性 */
    call    check_lm_support
    call    get_cpu_count
    call    check_smap_smep

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
    
    /* 1. 低半核恒等映射 (Identity Map): 0x0000000000000000 */
    /* PML4[0] -> PDPT */
    movl    $(PDPT_BASE|0x3), (PML4_BASE)
    movl    $0, (PML4_BASE+4)
    
    /* 2. 高半核直接映射 (HHDM): 0xFFFF800000000000 */
    /* PML4[256] -> PDPT (256 * 8 = 2048 = 0x800) */
    movl    $(PDPT_BASE|0x3), (PML4_BASE + 0x800)
    movl    $0, (PML4_BASE + 0x804)

    /* 链接 PDPT 到 PD0-PD3 (映射物理 0 - 4GB) */
    movl    $(PD0_BASE|0x3), (PDPT_BASE+0)
    movl    $0, (PDPT_BASE+4)
    movl    $(PD1_BASE|0x3), (PDPT_BASE+8)
    movl    $0, (PDPT_BASE+12)
    movl    $(PD2_BASE|0x3), (PDPT_BASE+16)
    movl    $0, (PDPT_BASE+20)
    movl    $(PD3_BASE|0x3), (PDPT_BASE+24)
    movl    $0, (PDPT_BASE+28)
    
    /* 2MB 大页映射 (PDE 属性: Present | R/W | Page Size = 0x83) */
    movl    $PD0_BASE, %edi
    movl    $0x00000083, %eax
    movl    $512, %ecx
.pd0: movl %eax,(%edi); movl $0,4(%edi); addl $0x200000,%eax; addl $8,%edi; loop .pd0
    movl    $PD1_BASE, %edi
    movl    $0x40000083, %eax
    movl    $512, %ecx
.pd1: movl %eax,(%edi); movl $0,4(%edi); addl $0x200000,%eax; addl $8,%edi; loop .pd1
    movl    $PD2_BASE, %edi
    movl    $0x80000083, %eax
    movl    $512, %ecx
.pd2: movl %eax,(%edi); movl $0,4(%edi); addl $0x200000,%eax; addl $8,%edi; loop .pd2
    movl    $PD3_BASE, %edi
    movl    $0xC0000083, %eax
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

/* 特性检测子程序 */
check_lm_support:
    pushfl
    popl    %eax
    movl    %eax, %ecx
    xorl    $(1 << 21), %eax
    pushl   %eax
    popfl
    pushfl
    popl    %eax
    pushl   %ecx
    popfl
    cmpl    %ecx, %eax
    je      .halt_no_lm
    movl    $0x80000000, %eax
    cpuid
    cmpl    $0x80000001, %eax
    jb      .halt_no_lm
    movl    $0x80000001, %eax
    cpuid
    testl   $(1 << 29), %edx
    jz      .halt_no_lm
    ret
.halt_no_lm:
    cli
    hlt
    jmp     .halt_no_lm

get_cpu_count:
    movl    $1, %eax
    cpuid
    testl   $(1 << 28), %edx    
    jz      .smp_single
    movl    %ebx, %eax
    shrl    $16, %eax
    andl    $0xFF, %eax
    testl   %eax, %eax
    jz      .smp_single
    movl    %eax, (BOOT_INFO + BI_CPU_COUNT)
    ret
.smp_single:
    movl    $1, (BOOT_INFO + BI_CPU_COUNT)
    ret

check_smap_smep:
    movl    $0, %eax
    cpuid
    cmpl    $7, %eax
    jb      .no_smap_smep
    movl    $7, %eax
    xorl    %ecx, %ecx
    cpuid
    testl   $(1 << 7), %ebx
    jz      .check_smap
    movl    %cr4, %eax
    orl     $(1 << 20), %eax
    movl    %eax, %cr4
.check_smap:
    testl   $(1 << 20), %ebx
    jz      .no_smap_smep
.no_smap_smep:
    ret

    .code64
lm64_entry:
    movw    $SEL_DATA64, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    
    /* 使用正确的 HHDM_OFFSET 切换指针 */
    movq    $(HHDM_OFFSET + 0x9FFF0), %rsp
    movq    $(HHDM_OFFSET + BOOT_INFO), %rdi
    
    /* 计算内核入口的高半核虚拟地址并跳转 */
    movq    $(HHDM_OFFSET + 0x100000), %rax
    jmpq    *%rax

.lm_halt:
    cli
    hlt
    jmp .lm_halt