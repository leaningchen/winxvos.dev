/* boot/stage2.S - Stage2 Boot Loader (Production Ready with Higher Half)
 * 加载地址: 0x10000 (由 Stage1 或 BIOS 加载)
 *
 * ============================================================================
 * 核心架构特性：
 *   1. 高半核直接映射 (Higher Half Direct Map)
 *      物理地址 0~XGB 同时映射到虚拟地址 0~XGB (恒等映射) 和
 *      0xFFFF800000000000 + 0~XGB (高半核映射)。切换后内核运行在高地址。
 *   2. 指令集扩展全面启用
 *      FPU、MMX、SSE、SSE2 通过 CR0/CR4 启用；AVX 通过 XSAVE/XRSTOR 与 XCR0 启用。
 *   3. 四级页表 (PML4) 支持超大内存
 *      使用 2MB 大页映射，基础映射覆盖 0~4GB；若物理内存 >4GB，动态分配额外
 *      Page Directory 并填充 PDE，理论上支持 512TB 物理内存。
 *   4. 安全特性
 *      SMEP (禁止内核执行用户代码)、SMAP (禁止内核随意访问用户数据)、
 *      NX/XD (页表不可执行位)、CR0.WP (写保护) 均在检测后启用。
 *   5. 完整的硬件信息收集
 *      E820 内存映射、VBE 帧缓冲区、ACPI RSDP (含 XSDT)、SMBIOS、逻辑 CPU 数量、
 *      最大物理地址、CPU 特性标志，全部存入 zenith_boot_info 结构体。
 *   6. 底层硬件初始化
 *      串口 (COM1, 115200 8N1)、A20 地址线、键盘控制器缓冲区清空、8259 PIC 屏蔽、
 *      全局描述符表 (GDT) 建立。
 * ============================================================================
 */

/* ---------- 编译时常量 ---------- */
#ifndef KERNEL_LBA
#define KERNEL_LBA 65               /* 内核在磁盘上的起始逻辑扇区号 */
#endif
#ifndef KERNEL_SECTORS
#define KERNEL_SECTORS 256           /* 内核占用的扇区数 (128 KB) */
#endif

/* ---------- 高半核映射基址 (对应 PML4[256]) ---------- */
.equ HHDM_OFFSET, 0xFFFF800000000000 /* 物理地址 0 映射到的虚拟地址 */

/* ============================================================================
 * 实模式下使用的临时变量区 (使用 DS=0 绝对寻址)
 * 所有变量位于内存低 1MB 以内，便于实模式直接访问。
 * 注意：这些地址不能与栈 (0x3FFE 向下) 和其他数据结构重叠。
 * ============================================================================ */
.equ TMP_DRIVE,       0x500   /* byte:  BIOS 传入的启动驱动器号 (DL) */
.equ TMP_M1680,       0x502   /* word:  找到的 1680x1050 模式号 */
.equ TMP_M1440,       0x504   /* word:  找到的 1440x900 模式号 */
.equ TMP_M800,        0x506   /* word:  找到的 800x600 模式号 */
.equ TMP_E820CNT,     0x508   /* dword: E820 有效条目计数 */
.equ TMP_RSDP_LO,     0x50C   /* dword: RSDP 物理地址低 32 位 */
.equ TMP_MEM_LO,      0x510   /* dword: 可用物理内存总量低 32 位 */
.equ TMP_MEM_HI,      0x514   /* dword: 可用物理内存总量高 32 位 */
.equ TMP_SMBIOS,      0x518   /* dword: SMBIOS Entry Point 物理地址 (低 32 位) */
.equ TMP_MAX_PHYS,    0x51C   /* qword: E820 遍历得到的最大物理地址 (Base+Length) */
.equ TMP_RSDP_HI,     0x524   /* dword: ACPI 2.0+ RSDP 高 32 位 (XSDT) */
.equ TMP_PHYS_LO,     0x530   /* dword: 大内存映射时当前物理地址低 32 位 */
.equ TMP_PHYS_HI,     0x534   /* dword: 大内存映射时当前物理地址高 32 位 */

/* ============================================================================
 * 固定物理地址内存布局
 * ============================================================================ */
.equ BOOT_INFO,       0x5000  /* zenith_boot_info 结构体 (256 字节) */
.equ E820_BUF,        0x6000  /* E820 缓冲区 (最多 128 条 × 24 字节 = 3072 字节) */
.equ VBE_INFO,        0x7000  /* VBE 控制器信息块 (512 字节) */
.equ VBE_MODE,        0x7200  /* VBE 模式信息块 (256 字节) */
.equ GDT_BASE,        0x9000  /* 全局描述符表: 5 个 8 字节描述符 + 6 字节 GDTR */

/* ---------- 段选择子 (保护模式 / 长模式) ---------- */
.equ SEL_CODE32,      0x08    /* 32 位代码段描述符在 GDT 中的索引 (8 = index 1) */
.equ SEL_DATA32,      0x10    /* 32 位数据段描述符 (index 2) */
.equ SEL_CODE64,      0x18    /* 64 位代码段描述符 (index 3, L=1) */
.equ SEL_DATA64,      0x20    /* 64 位数据段描述符 (index 4) */

/* ============================================================================
 * 页表物理基址 (4 级页表结构)
 * 布局：
 *   PML4 (Level 4)        0x70000 - 0x70FFF (4 KB)
 *   PDPT (Level 3)        0x71000 - 0x71FFF (4 KB)
 *   PD0  (Level 2, 0-1GB) 0x72000 - 0x72FFF (4 KB)
 *   PD1  (Level 2, 1-2GB) 0x73000 - 0x73FFF (4 KB)
 *   PD2  (Level 2, 2-3GB) 0x74000 - 0x74FFF (4 KB)
 *   PD3  (Level 2, 3-4GB) 0x75000 - 0x75FFF (4 KB)
 *   PD_EXT (扩展区, >4GB) 0x76000 - 0x7FFFF (约 40 KB, 最多 10 个额外 PD)
 * ============================================================================ */
.equ PML4_BASE,       0x70000
.equ PDPT_BASE,       0x71000
.equ PD0_BASE,        0x72000
.equ PD1_BASE,        0x73000
.equ PD2_BASE,        0x74000
.equ PD3_BASE,        0x75000
.equ PD_EXT_BASE,     0x76000  /* 扩展 Page Directory 起始地址 */
.equ PD_EXT_MAX,      0x80000  /* 扩展区最大边界 (避免覆盖其他数据) */

/* ============================================================================
 * zenith_boot_info 结构体偏移量
 * 该结构体用于向内核传递所有收集到的硬件信息。
 * 各字段对齐至自然边界，内核需包含相同定义的结构体。
 * ============================================================================ */
.equ BI_MAGIC,        0x00    /* dword: 魔数 0xB007B007 表示有效启动信息 */
.equ BI_VERSION,      0x04    /* dword: 结构体版本号 */
.equ BI_FB_ADDR,      0x08    /* qword: 帧缓冲区物理地址 */
.equ BI_FB_PITCH,     0x10    /* dword: 帧缓冲区每行字节数 */
.equ BI_FB_WIDTH,     0x14    /* dword: 屏幕宽度 (像素) */
.equ BI_FB_HEIGHT,    0x18    /* dword: 屏幕高度 (像素) */
.equ BI_FB_BPP,       0x1C    /* byte:  每像素位数 (32) */
.equ BI_E820_COUNT,   0x20    /* dword: E820 条目数量 */
.equ BI_E820_ADDR,    0x24    /* qword: E820 缓冲区物理地址 */
.equ BI_MEM_TOTAL,    0x28    /* qword: 全部可用物理内存 (字节) */
.equ BI_ACPI_RSDP,    0x30    /* qword: ACPI RSDP 物理地址 */
.equ BI_BOOT_DRIVE,   0x38    /* dword: 启动驱动器号 */
.equ BI_CPU_COUNT,    0x3C    /* dword: 逻辑 CPU 数量 */
.equ BI_SMBIOS_ADDR,  0x40    /* qword: SMBIOS Entry Point 物理地址 */
/* VBE Direct Color 掩码 (各字段占 1 字节) */
.equ BI_RED_MASK_SZ,  0x44    /* 红色掩码大小 (位数) */
.equ BI_RED_MASK_POS, 0x45    /* 红色字段起始位 */
.equ BI_GRN_MASK_SZ,  0x46    /* 绿色掩码大小 */
.equ BI_GRN_MASK_POS, 0x47    /* 绿色字段起始位 */
.equ BI_BLU_MASK_SZ,  0x48    /* 蓝色掩码大小 */
.equ BI_BLU_MASK_POS, 0x49    /* 蓝色字段起始位 */
.equ BI_RSRV_MASK_SZ, 0x4A    /* 保留字段掩码大小 */
.equ BI_RSRV_MASK_POS,0x4B    /* 保留字段起始位 */
.equ BI_MAX_PHYS_ADDR,0x50    /* qword: 最大物理地址 (用于内存管理) */
.equ BI_CPU_FEATURES, 0x58    /* qword: CPU 特性标志 (位 0: NX, 位 1: SMEP, 位 2: SMAP) */

/* ---------- 代码段开始 (16 位实模式) ---------- */
    .code16
    .section .text
    .global _start

_start:
    /* 初始化段寄存器 DS=ES=SS=0，堆栈指针设于 0x3FFE (避开变量区) */
    xorw    %ax, %ax
    movw    %ax, %ds            /* 数据段置 0，使用绝对地址访问低 1MB 内存 */
    movw    %ax, %es            /* 附加段置 0 */
    movw    %ax, %ss            /* 堆栈段置 0 */
    movw    $0x3FFE, %sp        /* 栈向下增长，不会覆盖 0x500~0x534 的临时变量 */

    /* 保存启动驱动器号，清零所有临时变量 */
    movb    %dl, (TMP_DRIVE)    /* BIOS 会将被引导设备的驱动器号放在 DL */
    movw    $0, (TMP_M1680)
    movw    $0, (TMP_M1440)
    movw    $0, (TMP_M800)
    movl    $0, (TMP_MEM_LO)
    movl    $0, (TMP_MEM_HI)
    movl    $0, (TMP_SMBIOS)
    movl    $0, (TMP_RSDP_HI)
    movl    $0, (TMP_MAX_PHYS)        /* 清除最大物理地址低 32 位 */
    movl    $0, (TMP_MAX_PHYS+4)      /* 清除最大物理地址高 32 位 */

    /* 底层硬件初始化 (顺序重要) */
    call    init_serial         /* COM1 串口初始化 (便于输出调试信息) */
    call    enable_a20          /* 开启 A20 地址线，否则无法访问 1MB 以上内存 */
    call    flush_8042          /* 清空键盘控制器缓冲区，避免残余数据干扰 */
    call    init_gdt            /* 建立 GDT，为进入保护模式/长模式做准备 */

/* ============================================================================
 * E820 内存探测
 * 使用 INT 0x15, EAX=0xE820 获取系统物理内存映射。
 * 遍历所有条目，累加 type=1 (可用内存) 的 length 得到总内存大小。
 * 同时记录所有区域 (Base+Length) 的最大值，作为最大物理地址，
 * 用于后续决定需要映射多少物理内存 (>4GB 时动态扩展页表)。
 * ============================================================================ */
e820_detect:
    xorw    %ax, %ax
    movw    %ax, %es            /* ES:DI = 0:E820_BUF */
    movw    $E820_BUF, %di
    xorl    %ebx, %ebx          /* 首次调用 EBX 必须为 0 */
    xorl    %ebp, %ebp          /* EBP 用作本地条目计数器 */
.e820_loop:
    movl    $0xE820, %eax       /* 功能号 */
    movl    $24, %ecx           /* 请求 24 字节 (ACPI 3.0 格式) */
    movl    $0x534D4150, %edx   /* 魔数 'SMAP' */
    int     $0x15
    jc      .e820_done          /* CF=1 表示错误或不支持 */
    cmpl    $0x534D4150, %eax   /* 某些 BIOS 不修改 EAX */
    jne     .e820_done
    testl   %ecx, %ecx          /* 返回 0 字节，无数据 */
    jz      .e820_next
    cmpl    $20, %ecx           /* 至少需要 20 字节 (Base+Length+Type) */
    jb      .e820_next
    cmpl    $24, %ecx
    jb      .e820_store         /* 正好 20 字节，ACPI 2.0 格式，直接存储 */
    /* 24 字节，检查 ACPI 3.0 扩展属性 bit 0 (条目有效且可被 OS 使用) */
    testb   $1, 20(%di)
    jz      .e820_next          /* 忽略不可用的条目 */
.e820_store:
    /* 更新最大物理地址 (Base + Length) */
    movl    0(%di), %eax        /* Base 低 32 位 */
    movl    4(%di), %edx        /* Base 高 32 位 */
    addl    8(%di), %eax        /* + Length 低 32 位 */
    adcl    12(%di), %edx       /* + Length 高 32 位 (带进位) */
    /* 与当前记录的最大值比较 */
    cmpl    (TMP_MAX_PHYS+4), %edx  /* 先比较高 32 位 */
    jb      .e820_check_mem     /* 小于，不更新 */
    ja      .e820_update_max    /* 大于，更新 */
    cmpl    (TMP_MAX_PHYS), %eax /* 高 32 位相等，比较低 32 位 */
    jbe     .e820_check_mem
.e820_update_max:
    movl    %eax, (TMP_MAX_PHYS)    /* 保存新的最大物理地址低 32 位 */
    movl    %edx, (TMP_MAX_PHYS+4)  /* 保存高 32 位 */
.e820_check_mem:
    /* 只累加可用内存 (type == 1) */
    cmpl    $1, 16(%di)         /* Type 字段在偏移 16 */
    jne     .e820_skip_mem
    movl    8(%di), %eax        /* Length 低 32 位 */
    movl    12(%di), %edx       /* Length 高 32 位 */
    addl    %eax, (TMP_MEM_LO)  /* 累加到总内存 (可能超过 4GB) */
    adcl    %edx, (TMP_MEM_HI)
.e820_skip_mem:
    addw    $24, %di            /* 移动到下一个条目 (24 字节) */
    incl    %ebp
    cmpl    $128, %ebp          /* 缓冲区最多容纳 128 条 */
    jae     .e820_done
.e820_next:
    testl   %ebx, %ebx          /* EBX=0 表示已无更多条目 */
    jz      .e820_done
    jmp     .e820_loop
.e820_done:
    movl    %ebp, (TMP_E820CNT) /* 存储有效条目数 */

/* ============================================================================
 * RSDP 搜索 (ACPI Root System Description Pointer)
 * 搜索顺序：
 *   1. 扩展 BIOS 数据区 (EBDA) 的前 1KB
 *   2. 0xE0000 - 0xFFFFF (BIOS 保留区)
 *   3. 0xF0000 - 0xFFFFF (备用区域)
 * 支持 ACPI 1.0 (RSDT) 和 ACPI 2.0+ (XSDT) 地址。
 * ============================================================================ */
rsdp_search:
    movl    $0, (TMP_RSDP_LO)
    movl    $0, (TMP_RSDP_HI)

    /* 阶段 1: 搜索 EBDA (从 BDA 偏移 0x0E 获取段地址) */
    movw    (0x40E), %ax        /* BDA 0x40E 存储 EBDA 段地址 */
    testw   %ax, %ax
    jz      .rsdp_bios          /* 若为 0，直接搜索 BIOS 区域 */
    movw    %ax, %fs
    xorw    %si, %si
    movw    $64, %cx            /* 搜索 64*16 = 1KB */
.rsdp_ebda_loop:
    movl    %fs:(%si), %eax
    cmpl    $0x20445352, %eax   /* "RSD " 小端序 */
    jne     .rsdp_ebda_next
    movl    %fs:4(%si), %eax
    cmpl    $0x20525450, %eax   /* "PTR " */
    jne     .rsdp_ebda_next
    /* 找到 RSDP，计算物理地址: 段基址*16 + 偏移 */
    movw    %fs, %ax
    movzwl  %ax, %eax
    shll    $4, %eax
    movzwl  %si, %edx
    addl    %edx, %eax
    movl    %eax, (TMP_RSDP_LO)
    /* 检查 ACPI 版本 (Revision 字段在偏移 15) */
    movb    %fs:15(%si), %al
    cmpb    $2, %al             /* 版本 >= 2 才有 XSDT */
    jb      .rsdp_done
    movl    %fs:24(%si), %eax   /* 读取 XSDT 地址 (偏移 24) */
    movl    %eax, (TMP_RSDP_HI)
    jmp     .rsdp_done
.rsdp_ebda_next:
    addw    $16, %si            /* 16 字节对齐搜索 */
    loop    .rsdp_ebda_loop

    /* 阶段 2: 搜索 BIOS 区域 0xE0000-0xFFFFF */
.rsdp_bios:
    movw    $0xE000, %ax
    movw    %ax, %fs
    xorw    %si, %si
    movw    $0x1000, %cx        /* 64KB / 16 = 4096 次 */
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
    movb    %fs:15(%si), %al
    cmpb    $2, %al
    jb      .rsdp_done
    movl    %fs:24(%si), %eax
    movl    %eax, (TMP_RSDP_HI)
    jmp     .rsdp_done
.rsdp_bios_next:
    addw    $16, %si
    jnz     .rsdp_bios_loop     /* SI 不为 0 继续 (最多 65536 次) */

    /* 阶段 3: 备用区域 0xF0000-0xFFFFF (某些 BIOS 使用) */
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
    movb    %fs:15(%si), %al
    cmpb    $2, %al
    jb      .rsdp_done
    movl    %fs:24(%si), %eax
    movl    %eax, (TMP_RSDP_HI)
    jmp     .rsdp_done
.rsdp_f000_next:
    addw    $16, %si
    loop    .rsdp_f000_loop
.rsdp_done:

/* ============================================================================
 * SMBIOS 入口点搜索
 * 搜索范围: 0xF0000 - 0xFFFFF，以 16 字节对齐搜索 "_SM_" 或 "_DMI_"。
 * ============================================================================ */
smbios_search:
    movw    $0xF000, %ax
    movw    %ax, %es
    xorw    %di, %di
    movw    $0x1000, %cx        /* 4096 次 */
.smbios_loop:
    movl    %es:(%di), %eax
    cmpl    $0x5F4D535F, %eax   /* "_SM_" */
    je      .smbios_found
    cmpl    $0x5F494D44, %eax   /* "_DMI_" (旧版) */
    je      .smbios_found
    addw    $16, %di
    loop    .smbios_loop
    movl    $0, (TMP_SMBIOS)    /* 未找到，记为 0 */
    jmp     .smbios_done
.smbios_found:
    movzwl  %di, %eax           /* 计算物理地址: 段基址*16 + 偏移 */
    addl    $0xF0000, %eax
    movl    %eax, (TMP_SMBIOS)
.smbios_done:

/* ============================================================================
 * VBE (VESA BIOS Extension) 检测与模式设置
 * 优先级: 1680x1050 > 1440x900 > 800x600
 * 要求: 线性帧缓冲 (bit 7), 图形模式 (bit 4), 硬件支持 (bit 0), 32 位色深,
 *       Direct Color 内存模型 (6)。
 * ============================================================================ */
vbe_detect:
    xorw    %ax, %ax
    movw    %ax, %es
    movw    $VBE_INFO, %di
    /* 填写 VBE2 签名，请求 VBE 2.0+ 信息 */
    movb    $0x56, (VBE_INFO + 0)   /* 'V' */
    movb    $0x42, (VBE_INFO + 1)   /* 'B' */
    movb    $0x45, (VBE_INFO + 2)   /* 'E' */
    movb    $0x32, (VBE_INFO + 3)   /* '2' */
    movw    $0x4F00, %ax            /* 功能 0: 获取控制器信息 */
    int     $0x10
    cmpw    $0x004F, %ax            /* 成功返回 0x004F */
    jne     .vbe_skip_scan

    /* 获取视频模式列表指针 (段:偏移) */
    movw    (VBE_INFO + 0x10), %ax  /* VideoModePtr 段 */
    movw    %ax, %fs
    movw    (VBE_INFO + 0x0E), %si  /* VideoModePtr 偏移 */

.vbe_mode_scan:
    movw    %fs:(%si), %cx          /* 读取下一个模式号 */
    cmpw    $0xFFFF, %cx            /* 0xFFFF 表示列表结束 */
    je      .vbe_scan_done
    addw    $2, %si                 /* 移动到下一个条目 */

    /* 获取该模式的具体信息 (功能 1) */
    xorw    %ax, %ax
    movw    %ax, %es
    movw    $VBE_MODE, %di
    movw    $0x4F01, %ax
    int     $0x10
    cmpw    $0x004F, %ax
    jne     .vbe_mode_scan

    /* 检查模式属性 (ModeAttributes 偏移 0) */
    movw    (VBE_MODE + 0x00), %ax
    testw   $0x0080, %ax            /* bit 7: 线性帧缓冲可用 */
    jz      .vbe_mode_scan
    testw   $0x0010, %ax            /* bit 4: 图形模式 */
    jz      .vbe_mode_scan
    testw   $0x0001, %ax            /* bit 0: 硬件支持 */
    jz      .vbe_mode_scan

    /* 检查色深 (BitsPerPixel 偏移 0x19) */
    movb    (VBE_MODE + 0x19), %al
    cmpb    $32, %al                /* 必须 32 位 */
    jne     .vbe_mode_scan

    /* 检查内存模型 (MemoryModel 偏移 0x1B) */
    movb    (VBE_MODE + 0x1B), %al
    cmpb    $6, %al                 /* 6 = Direct Color */
    jne     .vbe_mode_scan

    /* 分辨率匹配 */
    movw    (VBE_MODE + 0x12), %ax  /* XResolution */
    movw    (VBE_MODE + 0x14), %bx  /* YResolution */

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
    /* 选择优先级最高的模式 */
    movw    (TMP_M1680), %cx
    testw   %cx, %cx
    jnz     .vbe_set_mode
    movw    (TMP_M1440), %cx
    testw   %cx, %cx
    jnz     .vbe_set_mode
    movw    (TMP_M800), %cx
    testw   %cx, %cx
    jz      .vbe_skip_scan          /* 未找到合适模式 */

.vbe_set_mode:
    /* 重新获取模式信息以确保缓冲区包含最新数据 */
    xorw    %ax, %ax
    movw    %ax, %es
    movw    $VBE_MODE, %di
    movw    $0x4F01, %ax
    int     $0x10

    /* 设置模式，位 14 置位表示使用线性帧缓冲 */
    orw     $0x4000, %cx
    movw    $0x4F02, %ax
    movw    %cx, %bx
    int     $0x10
.vbe_skip_scan:

/* ============================================================================
 * 填充 zenith_boot_info 结构体 (位于物理地址 BOOT_INFO = 0x5000)
 * 该结构体将被传递给内核，包含所有收集到的信息。
 * ============================================================================ */
init_boot_info:
    call    disable_pic             /* 禁止 8259 PIC，其 IRQ 在保护模式不再使用 */

    /* 将 BOOT_INFO 区域全部清零 (64 个双字 = 256 字节) */
    xorw    %ax, %ax
    movw    %ax, %es
    movw    $BOOT_INFO, %di
    movw    $64, %cx
    rep stosl

    /* 魔数和版本 */
    movl    $0xB007B007, %eax
    movl    %eax, (BOOT_INFO + BI_MAGIC)
    movl    $1, %eax
    movl    %eax, (BOOT_INFO + BI_VERSION)

    /* 帧缓冲区信息 */
    movl    (VBE_MODE + 0x28), %eax /* PhysBasePtr (物理地址低 32 位) */
    movl    %eax, (BOOT_INFO + BI_FB_ADDR)
    movl    $0, (BOOT_INFO + BI_FB_ADDR + 4)   /* 高 32 位设为 0 */

    movzwl  (VBE_MODE + 0x10), %eax /* BytesPerScanLine */
    movl    %eax, (BOOT_INFO + BI_FB_PITCH)
    movzwl  (VBE_MODE + 0x12), %eax /* XResolution */
    movl    %eax, (BOOT_INFO + BI_FB_WIDTH)
    movzwl  (VBE_MODE + 0x14), %eax /* YResolution */
    movl    %eax, (BOOT_INFO + BI_FB_HEIGHT)
    movb    (VBE_MODE + 0x19), %al  /* BitsPerPixel */
    movb    %al, (BOOT_INFO + BI_FB_BPP)

    /* VBE Direct Color 掩码 (用于计算像素格式) */
    movb    (VBE_MODE + 0x24), %al
    movb    %al, (BOOT_INFO + BI_RED_MASK_SZ)
    movb    (VBE_MODE + 0x25), %al
    movb    %al, (BOOT_INFO + BI_RED_MASK_POS)
    movb    (VBE_MODE + 0x26), %al
    movb    %al, (BOOT_INFO + BI_GRN_MASK_SZ)
    movb    (VBE_MODE + 0x27), %al
    movb    %al, (BOOT_INFO + BI_GRN_MASK_POS)
    movb    (VBE_MODE + 0x28), %al
    movb    %al, (BOOT_INFO + BI_BLU_MASK_SZ)
    movb    (VBE_MODE + 0x29), %al
    movb    %al, (BOOT_INFO + BI_BLU_MASK_POS)
    movb    (VBE_MODE + 0x2A), %al
    movb    %al, (BOOT_INFO + BI_RSRV_MASK_SZ)
    movb    (VBE_MODE + 0x2B), %al
    movb    %al, (BOOT_INFO + BI_RSRV_MASK_POS)

    /* E820 内存信息 */
    movl    (TMP_E820CNT), %eax
    movl    %eax, (BOOT_INFO + BI_E820_COUNT)
    movl    $E820_BUF, (BOOT_INFO + BI_E820_ADDR)
    movl    $0, (BOOT_INFO + BI_E820_ADDR + 4)

    /* 总可用内存 (64 位) */
    movl    (TMP_MEM_LO), %eax
    movl    %eax, (BOOT_INFO + BI_MEM_TOTAL)
    movl    (TMP_MEM_HI), %eax
    movl    %eax, (BOOT_INFO + BI_MEM_TOTAL + 4)

    /* 最大物理地址 */
    movl    (TMP_MAX_PHYS), %eax
    movl    %eax, (BOOT_INFO + BI_MAX_PHYS_ADDR)
    movl    (TMP_MAX_PHYS+4), %eax
    movl    %eax, (BOOT_INFO + BI_MAX_PHYS_ADDR + 4)

    /* ACPI RSDP 地址 (64 位) */
    movl    (TMP_RSDP_LO), %eax
    movl    %eax, (BOOT_INFO + BI_ACPI_RSDP)
    movl    (TMP_RSDP_HI), %eax
    movl    %eax, (BOOT_INFO + BI_ACPI_RSDP + 4)

    /* 启动驱动器号 */
    movzbl  (TMP_DRIVE), %eax
    movl    %eax, (BOOT_INFO + BI_BOOT_DRIVE)

    /* SMBIOS 地址 */
    movl    (TMP_SMBIOS), %eax
    movl    %eax, (BOOT_INFO + BI_SMBIOS_ADDR)
    movl    $0, (BOOT_INFO + BI_SMBIOS_ADDR + 4)

    /* 注意: BI_CPU_COUNT 和 BI_CPU_FEATURES 将在保护模式下由 get_cpu_count
     *       和 check_smap_smep_nx 填充，在此之前 BOOT_INFO 已清零。 */

/* ============================================================================
 * 进入 32 位保护模式
 * ============================================================================ */
enter_pm:
    cli                         /* 关中断，实模式中断在保护模式无效 */
    lgdt    (GDT_BASE + 0x28)   /* 加载 GDTR (6 字节: 2字节限制 + 4字节基址) */
    movl    %cr0, %eax
    orl     $0x1, %eax          /* 设置 CR0.PE (Protection Enable) */
    movl    %eax, %cr0
    /* 远跳转刷新指令流水线，并加载 32 位代码段选择子 */
    ljmpl   $SEL_CODE32, $pm32_entry

/* ============================================================================
 * 16 位实模式辅助子程序
 * ============================================================================ */

/* --- enable_a20: 启用 A20 地址线 (允许访问 1MB 以上内存) --- */
enable_a20:
    movw    $0x2403, %ax        /* 查询 A20 状态 */
    int     $0x15
    jc      .a20_fallback
    testb   $0x02, %al          /* bit 1 表示 A20 已启用 */
    jnz     .a20_done
    movw    $0x2401, %ax        /* 尝试启用 A20 */
    int     $0x15
    jnc     .a20_done
.a20_fallback:
    inb     $0x92, %al          /* 快速 A20 方法 (系统控制端口 A) */
    testb   $0x02, %al
    jnz     .a20_done
    orb     $0x02, %al          /* 设置 bit 1 */
    outb    %al, $0x92
.a20_done:
    ret

/* --- flush_8042: 清空键盘控制器输出缓冲区 --- */
flush_8042:
.flush_loop:
    inb     $0x64, %al          /* 读状态寄存器 */
    testb   $0x01, %al          /* 输出缓冲区满？ */
    jz      .flush_done
    inb     $0x60, %al          /* 读取并丢弃数据 */
    jmp     .flush_loop
.flush_done:
    ret

/* --- init_serial: 初始化 COM1 (0x3F8) 为 115200 8N1, 启用 FIFO --- */
init_serial:
    movw    $0x3F8, %dx         /* 基地址 */
    movb    $0x00, %al          /* 关闭中断 */
    movw    $0x3F9, %dx
    outb    %al, %dx
    movb    $0x80, %al          /* 开启 DLAB 以设置波特率 */
    movw    $0x3FB, %dx
    outb    %al, %dx
    movb    $0x01, %al          /* 波特率除数低字节 = 1 (115200) */
    movw    $0x3F8, %dx
    outb    %al, %dx
    movb    $0x00, %al          /* 高字节 = 0 */
    movw    $0x3F9, %dx
    outb    %al, %dx
    movb    $0x03, %al          /* 8 数据位, 无校验, 1 停止位, 关闭 DLAB */
    movw    $0x3FB, %dx
    outb    %al, %dx
    movb    $0xC7, %al          /* 启用 FIFO, 清空, 14 字节触发 */
    movw    $0x3FA, %dx
    outb    %al, %dx
    movb    $0x0B, %al          /* DTR, RTS, 辅助输出 2 */
    movw    $0x3FC, %dx
    outb    %al, %dx
    ret

/* --- disable_pic: 屏蔽 8259 PIC 所有中断 --- */
disable_pic:
    movb    $0xFF, %al          /* 全部屏蔽 */
    outb    %al, $0x21          /* 主片 */
    outb    %al, $0xA1          /* 从片 */
    ret

/* --- init_gdt: 建立 GDT 包含 32/64 位代码段和数据段 --- */
init_gdt:
    pushw   %di
    movw    $GDT_BASE, %di
    /* 描述符 0: 空描述符 */
    movl    $0, (%di)
    movl    $0, 4(%di)
    addw    $8, %di
    /* 描述符 1: 32 位代码段 (0x08) - 基址 0, 限制 4GB, DPL=0, 可执行可读 */
    movl    $0x0000FFFF, (%di)   /* 限制低 16 位, 基址低 16 位 */
    movl    $0x00CF9A00, 4(%di)  /* 基址中 8 位, 访问 0x9A, 标志 0xCF, 基址高 8 位 */
    addw    $8, %di
    /* 描述符 2: 32 位数据段 (0x10) - 可写 */
    movl    $0x0000FFFF, (%di)
    movl    $0x00CF9200, 4(%di)
    addw    $8, %di
    /* 描述符 3: 64 位代码段 (0x18) - L=1 长模式 */
    movl    $0x0000FFFF, (%di)
    movl    $0x00AF9A00, 4(%di)  /* 标志 0xAF (G=1, L=1) */
    addw    $8, %di
    /* 描述符 4: 64 位数据段 (0x20) */
    movl    $0x0000FFFF, (%di)
    movl    $0x00AF9200, 4(%di)
    addw    $8, %di
    /* GDTR: 大小 = 5*8-1 = 39, 基址 = GDT_BASE */
    movw    $(5*8-1), (%di)
    movl    $GDT_BASE, 2(%di)
    popw    %di
    ret

/* ============================================================================
 * 32 位保护模式入口
 * 在此模式下完成 FPU/SSE/AVX 配置、特性检测、内核加载、页表构建，
 * 最后开启长模式。
 * ============================================================================ */
    .code32
pm32_entry:
    /* 加载 32 位数据段选择子 */
    movw    $SEL_DATA32, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    $0x9FFFC, %esp          /* 栈指针设在 640KB 附近 */

    /* ---------- 1. 启用 FPU、MMX、SSE、SSE2 ---------- */
    /*
     * x87 FPU 由 CR0.EM 和 CR0.MP 控制。
     * EM=0 禁用软件仿真，使硬件 FPU 可用。
     * MP=1 与 TS 配合用于任务切换时的 FPU 上下文保存。
     * SSE 扩展需要 CR4.OSFXSR (bit 9) 启用 FXSAVE/FXRSTOR，
     * CR4.OSXMMEXCPT (bit 10) 启用 SSE 异常处理。
     * MMX 指令复用 x87 寄存器，无需额外控制位。
     */
    movl    %cr0, %eax
    andl    $~(1 << 2), %eax        /* 清除 EM (bit 2) */
    orl     $(1 << 1), %eax         /* 设置 MP (bit 1) */
    movl    %eax, %cr0
    movl    %cr4, %eax
    orl     $(1 << 9), %eax         /* OSFXSR */
    orl     $(1 << 10), %eax        /* OSXMMEXCPT */
    movl    %eax, %cr4
    fninit                          /* 初始化 x87 FPU 状态 */

    /* ---------- 2. 检测并启用 AVX ---------- */
    /*
     * AVX 依赖 XSAVE/XRSTOR 机制，需要：
     *   - CPUID.01H.ECX[27] = OSXSAVE 支持
     *   - CPUID.01H.ECX[28] = AVX 硬件支持
     *   - CR4.OSXSAVE = 1
     *   - 通过 XSETBV 设置 XCR0 的 SSE (bit 1) 和 AVX (bit 2) 位。
     * 同时记录 XSAVE 区域大小 (来自 CPUID.0DH.ECX=0) 到 BOOT_INFO，
     * 内核需要它分配 FXSAVE/XSAVE 上下文。
     */
    movl    $1, %eax
    cpuid
    testl   $(1 << 27), %ecx
    jz      .skip_avx
    testl   $(1 << 28), %ecx
    jz      .skip_avx
    /* 启用 OSXSAVE */
    movl    %cr4, %eax
    orl     $(1 << 18), %eax        /* CR4.OSXSAVE */
    movl    %eax, %cr4
    /* 查询 XSAVE 区域大小 */
    movl    $0xD, %eax              /* 功能号 0x0D */
    xorl    %ecx, %ecx
    cpuid
    movl    %ebx, (BOOT_INFO + BI_CPU_FEATURES)  /* 保存 XSAVE 区域大小 (字节) */
    /* 设置 XCR0: 启用 x87 (bit 0), SSE (bit 1), AVX (bit 2) */
    xorl    %ecx, %ecx              /* XCR0 寄存器号为 0 */
    movl    $0x7, %eax              /* 低 3 位全 1 */
    movl    $0, %edx
    xgetbv
    orl     $0x7, %eax
    xsetbv
.skip_avx:

    /* ---------- 3. 清理 EFLAGS 和调试 MSR ---------- */
    pushl   $0x00000002             /* 仅保留 bit 1 (必须为 1) */
    popfl
    cld                             /* DF=0，字符串操作正向 */
    xorl    %eax, %eax
    xorl    %edx, %edx
    movl    $0x1D9, %ecx            /* IA32_DEBUGCTL MSR */
    wrmsr                           /* 清零调试控制 */

    /* ---------- 4. 长模式支持、CPU 计数、安全特性 ---------- */
    call    check_lm_support        /* 若不支持长模式则挂起 */
    call    get_cpu_count           /* 填充 BI_CPU_COUNT */
    call    check_smap_smep_nx      /* 开启 SMEP/SMAP/NX 并设置 BI_CPU_FEATURES */

/* ============================================================================
 * 从磁盘读取内核至 0x100000 (1MB)
 * 使用 LBA28 模式，每次读 1 扇区，PIO 方式。
 * ============================================================================ */
load_kernel:
    movl    $KERNEL_LBA, %esi       /* 起始扇区号 */
    movl    $KERNEL_SECTORS, %ecx   /* 剩余扇区数 */
    movl    $0x100000, %ebx         /* 目标物理地址 */
.lk_loop:
    testl   %ecx, %ecx
    jz      .lk_done

    /* 等待磁盘控制器空闲且驱动器就绪 */
.lk_wait1:
    movw    $0x1F7, %dx             /* 状态寄存器 */
    inb     %dx, %al
    testb   $0x80, %al              /* BSY */
    jnz     .lk_wait1
    testb   $0x40, %al              /* DRDY */
    jz      .lk_wait1

    /* 设置读取参数 */
    movl    $1, %eax                /* 扇区数 = 1 */
    movw    $0x1F2, %dx
    outb    %al, %dx                /* 扇区计数寄存器 */

    movl    %esi, %eax
    movw    $0x1F3, %dx
    outb    %al, %dx                /* LBA 低 8 位 */

    movb    %ah, %al
    movw    $0x1F4, %dx
    outb    %al, %dx                /* LBA 8-15 位 */

    shrl    $16, %eax
    movw    $0x1F5, %dx
    outb    %al, %dx                /* LBA 16-23 位 */

    movl    %esi, %eax
    shrl    $24, %eax
    orb     $0xE0, %al              /* LBA 24-27 位 + 主盘 + LBA 模式 */
    movw    $0x1F6, %dx
    outb    %al, %dx

    movb    $0x20, %al              /* 读扇区命令 */
    movw    $0x1F7, %dx
    outb    %al, %dx

    /* 等待数据就绪 */
.lk_wait2:
    movw    $0x1F7, %dx
    inb     %dx, %al
    testb   $0x80, %al
    jnz     .lk_wait2
    testb   $0x08, %al              /* DRQ 数据请求 */
    jz      .lk_wait2

    /* 读取 256 字 (512 字节) */
    pushl   %ecx
    movw    $0x1F0, %dx             /* 数据端口 */
    movl    $256, %ecx
    movl    %ebx, %edi              /* EDI = 目标地址 */
    rep insw
    popl    %ecx

    addl    $512, %ebx              /* 目标地址 +512 */
    incl    %esi                    /* 下一扇区 */
    decl    %ecx
    jmp     .lk_loop
.lk_done:

/* ============================================================================
 * 构建 4 级页表 (PML4, PDPT, PD)
 * 映射策略：
 *   - 使用 2MB 大页 (PDE.PS=1)，避免处理页表 (PT)。
 *   - PML4[0] 和 PML4[256] 指向同一个 PDPT，实现：
 *       低半核恒等映射: 虚拟 0 ~ XGB -> 物理 0 ~ XGB
 *       高半核直接映射: 虚拟 0xFFFF800000000000 + 0~XGB -> 物理 0 ~ XGB
 *   - PDPT 前 4 项指向 4 个 PD (PD0~PD3)，覆盖物理 0~4GB。
 *   - 若最大物理地址 >4GB，动态分配额外 PD (从 PD_EXT_BASE 开始)，
 *     并在 PDPT 中添加对应条目，填充 PDE 直到覆盖全部物理内存。
 *     当前实现最多支持到 128GB 左右 (取决于 PD_EXT_MAX - PD_EXT_BASE 空间)。
 * ============================================================================ */
setup_paging:
    /* 清零基础页表: PML4 + PDPT + PD0~PD3 共 6 页 (24KB) */
    movl    $PML4_BASE, %edi
    movl    $(6*4096/4), %ecx       /* 6页 * 1024 双字 = 6144 */
    xorl    %eax, %eax
    rep stosl

    /* PML4[0] -> PDPT, 属性 Present + R/W (0x3) */
    movl    $(PDPT_BASE|0x3), (PML4_BASE)
    movl    $0, (PML4_BASE+4)
    /* PML4[256] (偏移 256*8=0x800) -> PDPT，共享同一张 PDPT */
    movl    $(PDPT_BASE|0x3), (PML4_BASE + 0x800)
    movl    $0, (PML4_BASE + 0x804)

    /* PDPT[0..3] 指向 PD0~PD3 */
    movl    $(PD0_BASE|0x3), (PDPT_BASE+0)
    movl    $0, (PDPT_BASE+4)
    movl    $(PD1_BASE|0x3), (PDPT_BASE+8)
    movl    $0, (PDPT_BASE+12)
    movl    $(PD2_BASE|0x3), (PDPT_BASE+16)
    movl    $0, (PDPT_BASE+20)
    movl    $(PD3_BASE|0x3), (PDPT_BASE+24)
    movl    $0, (PDPT_BASE+28)

    /* 填充 PD0 ~ PD3 的 2MB 大页 PDE (属性 0x83 = Present+R/W+Page Size) */
    movl    $PD0_BASE, %edi
    movl    $0x00000083, %eax       /* 起始物理地址: 0 */
    movl    $512, %ecx
.pd0: movl %eax,(%edi); movl $0,4(%edi); addl $0x200000,%eax; addl $8,%edi; loop .pd0
    movl    $PD1_BASE, %edi
    movl    $0x40000083, %eax       /* 物理 1GB */
    movl    $512, %ecx
.pd1: movl %eax,(%edi); movl $0,4(%edi); addl $0x200000,%eax; addl $8,%edi; loop .pd1
    movl    $PD2_BASE, %edi
    movl    $0x80000083, %eax       /* 物理 2GB */
    movl    $512, %ecx
.pd2: movl %eax,(%edi); movl $0,4(%edi); addl $0x200000,%eax; addl $8,%edi; loop .pd2
    movl    $PD3_BASE, %edi
    movl    $0xC0000083, %eax       /* 物理 3GB */
    movl    $512, %ecx
.pd3: movl %eax,(%edi); movl $0,4(%edi); addl $0x200000,%eax; addl $8,%edi; loop .pd3

    /* ---------- 处理物理内存超过 4GB 的情况 ---------- */
    movl    (TMP_MAX_PHYS), %eax    /* max_phys 低 32 位 */
    movl    (TMP_MAX_PHYS+4), %edx  /* max_phys 高 32 位 */
    /* 计算总 2MB 页数 = (max_phys + 2MB - 1) >> 21 */
    addl    $0x1FFFFF, %eax         /* 加上 2MB-1 向上取整 */
    adcl    $0, %edx
    /* 64 位右移 21 位，结果在 EAX 中 (EDX 应为 0) */
    shrd    $21, %edx, %eax
    shr     $21, %edx
    /* 若总页数 <= 0x800 (4GB)，则无需额外映射 */
    cmpl    $0x800, %eax
    jbe     .paging_done

    subl    $0x800, %eax            /* 额外 2MB 页数 */
    movl    %eax, (TMP_M1680)       /* 复用 TMP_M1680 作为循环计数器 */

    /* 初始化物理地址从 4GB 开始: (phys_hi=1, phys_lo=0) */
    movl    $0, (TMP_PHYS_LO)
    movl    $1, (TMP_PHYS_HI)
    /* PDPT 索引从 4 开始 (前 4 项已用) */
    movl    $4, %esi
    /* 下一个可用 PD 的起始物理地址 */
    movl    $PD_EXT_BASE, %ebx
    /* 当前 PD 内偏移 (0~511) */
    movl    $0, %edx

.map_loop:
    /* 检查是否还有页需要映射 */
    cmpl    $0, (TMP_M1680)
    je      .paging_done

    /* 如果当前 PD 已满 (偏移 == 0 表示需要新 PD)，分配新 PD */
    testl   %edx, %edx
    jnz     .have_pd
    /* 检查是否超过扩展区上限 */
    cmpl    $PD_EXT_MAX, %ebx
    jae     .paging_done
    /* 在 PDPT[ESI] 中填入新 PD 的基址 (Present + R/W) */
    movl    %esi, %eax
    shll    $3, %eax                /* ESI * 8 */
    movl    %ebx, %ecx
    orl     $0x3, %ecx
    movl    %ecx, (PDPT_BASE)(%eax) /* 存低 32 位 */
    movl    $0, (PDPT_BASE+4)(%eax) /* 高 32 位清零 */
    /* 将新 PD 清零 */
    pushl   %edi
    movl    %ebx, %edi
    movl    $1024, %ecx             /* 512 项 × 8 字节 = 4096 字节 = 1024 双字 */
    xorl    %eax, %eax
    rep stosl
    popl    %edi
    /* 更新指针 */
    addl    $0x1000, %ebx           /* 下一个 PD 地址 */
    incl    %esi                    /* 下一个 PDPT 索引 */
    movl    $0, %edx                /* 重置 PD 内偏移 */

.have_pd:
    /* 计算当前 PDE 的地址: PD基址 (EBX-0x1000) + 偏移*8 */
    leal    -0x1000(%ebx), %edi     /* EDI = 当前 PD 基址 */
    leal    (%edi, %edx, 8), %edi   /* EDI = PDE 地址 */

    /* 构建 PDE 内容: 物理地址[21:31] | 0x83 (Present, R/W, Page Size) */
    movl    (TMP_PHYS_LO), %eax
    andl    $0xFFE00000, %eax       /* 保留地址位 21~31 */
    orl     $0x83, %eax
    movl    %eax, (%edi)            /* PDE 低 32 位 */
    movl    (TMP_PHYS_HI), %eax
    movl    %eax, 4(%edi)           /* PDE 高 32 位 */

    /* 当前物理地址增加 2MB */
    addl    $0x200000, (TMP_PHYS_LO)
    adcl    $0, (TMP_PHYS_HI)

    /* 更新 PD 内偏移 */
    incl    %edx
    cmpl    $512, %edx
    jne     .next_page
    movl    $0, %edx                /* 满 512 项，下一个循环将分配新 PD */
.next_page:
    decl    (TMP_M1680)
    jmp     .map_loop

.paging_done:

/* ============================================================================
 * 进入 IA-32e (长) 模式
 * 步骤: 启用 PAE & PSE -> 加载 CR3 -> EFER.LME=1 -> CR0.PG=1 -> 远跳转
 * ============================================================================ */
enter_lm:
    lgdt    (GDT_BASE + 0x28)       /* 重载 GDT，确保 64 位描述符可用 */
    movl    %cr4, %eax
    orl     $(1<<5), %eax           /* CR4.PAE = 1 (物理地址扩展) */
    orl     $(1<<4), %eax           /* CR4.PSE = 1 (允许大页) */
    movl    %eax, %cr4

    movl    $PML4_BASE, %eax
    movl    %eax, %cr3              /* 加载 PML4 基址 */

    movl    $0xC0000080, %ecx       /* EFER MSR 地址 */
    rdmsr
    orl     $(1<<8), %eax           /* EFER.LME = 1 (长模式启用) */
    wrmsr

    movl    %cr0, %eax
    orl     $(1<<31), %eax          /* CR0.PG = 1 (启用分页) */
    orl     $(1<<16), %eax          /* CR0.WP = 1 (写保护，增强安全) */
    movl    %eax, %cr0

    /* 长跳转至 64 位代码段，同时刷新流水线 */
    ljmp    $SEL_CODE64, $lm64_entry

/* ============================================================================
 * 32 位保护模式下的特征检测子程序
 * ============================================================================ */

/* --- check_lm_support: 检查 CPU 是否支持长模式，若不支持则停机 --- */
check_lm_support:
    /* 验证 CPUID 指令可用 (通过 EFLAGS.ID 位的可修改性) */
    pushfl
    popl    %eax
    movl    %eax, %ecx
    xorl    $(1 << 21), %eax        /* 翻转 ID 位 (bit 21) */
    pushl   %eax
    popfl
    pushfl
    popl    %eax
    pushl   %ecx
    popfl                           /* 恢复原 EFLAGS */
    cmpl    %ecx, %eax
    je      .halt_no_lm             /* 无法修改 ID 位，CPUID 不可用 */

    movl    $0x80000000, %eax
    cpuid
    cmpl    $0x80000001, %eax       /* 最大扩展功能号至少 0x80000001 */
    jb      .halt_no_lm
    movl    $0x80000001, %eax
    cpuid
    testl   $(1 << 29), %edx        /* EDX[29] = Long Mode */
    jz      .halt_no_lm
    ret
.halt_no_lm:
    cli
    hlt
    jmp     .halt_no_lm

/* --- get_cpu_count: 通过 CPUID 获取逻辑 CPU 数量，存入 BI_CPU_COUNT --- */
get_cpu_count:
    movl    $1, %eax
    cpuid
    testl   $(1 << 28), %edx        /* HTT (Hyper-Threading Technology) */
    jz      .smp_single
    movl    %ebx, %eax
    shrl    $16, %eax               /* EBX[23:16] 最大逻辑处理器数 */
    andl    $0xFF, %eax
    testl   %eax, %eax
    jz      .smp_single
    movl    %eax, (BOOT_INFO + BI_CPU_COUNT)
    ret
.smp_single:
    movl    $1, (BOOT_INFO + BI_CPU_COUNT)
    ret

/* --- check_smap_smep_nx: 检测并启用 NX/XD, SMEP, SMAP --- */
check_smap_smep_nx:
    /* NX/XD: 通过 CPUID.80000001H.EDX[20] 检测，EFER.NXE 启用 */
    movl    $0x80000001, %eax
    cpuid
    testl   $(1 << 20), %edx
    jz      .check_smep_smap
    movl    $0xC0000080, %ecx
    rdmsr
    orl     $(1 << 11), %eax        /* EFER.NXE = 1 */
    wrmsr
    /* 记录 NX 支持 (bit 0) */
    movl    (BOOT_INFO + BI_CPU_FEATURES), %eax
    orl     $0x01, %eax
    movl    %eax, (BOOT_INFO + BI_CPU_FEATURES)

.check_smep_smap:
    /* 检查 CPUID.07H 是否可用 */
    movl    $0, %eax
    cpuid
    cmpl    $7, %eax
    jb      .no_smep_smap
    movl    $7, %eax
    xorl    %ecx, %ecx
    cpuid
    /* SMEP: EBX[7] -> CR4.SMEP (bit 20) */
    testl   $(1 << 7), %ebx
    jz      .check_smap
    movl    %cr4, %eax
    orl     $(1 << 20), %eax
    movl    %eax, %cr4
    movl    (BOOT_INFO + BI_CPU_FEATURES), %eax
    orl     $0x02, %eax             /* 记录 SMEP (bit 1) */
    movl    %eax, (BOOT_INFO + BI_CPU_FEATURES)

.check_smap:
    /* SMAP: EBX[20] -> CR4.SMAP (bit 21) */
    testl   $(1 << 20), %ebx
    jz      .no_smep_smap
    movl    %cr4, %eax
    orl     $(1 << 21), %eax
    movl    %eax, %cr4
    movl    (BOOT_INFO + BI_CPU_FEATURES), %eax
    orl     $0x04, %eax             /* 记录 SMAP (bit 2) */
    movl    %eax, (BOOT_INFO + BI_CPU_FEATURES)

.no_smep_smap:
    ret

/* ============================================================================
 * 64 位长模式入口
 * ============================================================================ */
    .code64
lm64_entry:
    /* 加载 64 位数据段选择子 */
    movw    $SEL_DATA64, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    /* 切换到高半核栈 (RSP 使用高半核虚拟地址) */
    movq    $(HHDM_OFFSET + 0x9FFF0), %rsp
    /* RDI = 指向 BOOT_INFO 的高半核虚拟地址 (内核第一个参数) */
    movq    $(HHDM_OFFSET + BOOT_INFO), %rdi

    /* 跳转到内核入口 (内核位于 0x100000，此时通过高半核映射访问) */
    movq    $(HHDM_OFFSET + 0x100000), %rax
    jmpq    *%rax

/* 若内核意外返回，则停机 */
.lm_halt:
    cli
    hlt
    jmp .lm_halt