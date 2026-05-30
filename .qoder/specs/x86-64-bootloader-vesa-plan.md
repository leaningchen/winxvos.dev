# x86-64 Bootloader with VESA 图形模式 + SMP 多核初始化 — 实现计划

## Context

从零构建一个完整的 x86-64 两阶段 bootloader，使用 GNU AS (GAS) AT&T 语法编写。目标功能：

- 实模式 → 32 位保护模式 → 64 位长模式完整切换流程
- BIOS INT 15h/E820 探测物理内存映射
- VESA/VBE 2.0 图形模式：优先 1680×1050×32bpp，自动回退 1440×900，再回退 800×600
- C 运行时初始化（BSS 清零、64 位栈建立）
- SMP 多核初始化：ACPI MADT 发现所有 AP（Application Processors），LAPIC SIPI 序列唤醒，AP 进入 64 位长模式等待循环
- 图形界面显示：可用内存总量、已初始化 CPU 核心数、E820 条目列表

目标运行环境：QEMU、Bochs、真实 x86-64 硬件

---

## 整体启动流程

```
BIOS → Stage1@0x7C00 (16bit) → Stage2@0x10000 (16bit→32bit→64bit) → Kernel@0x100000 (64bit C)
         加载Stage2             E820/VBE/RSDP探测                      BSP初始化+唤醒AP
                                构建GDT/页表/切换模式
```

---

## 内存布局（物理地址）

```
地址范围                    内容
0x00000000~0x000003FF      IVT（实模式中断向量表）
0x00000400~0x000004FF      BDA（BIOS 数据区）
0x00005000                 BootInfo 结构体（128B，固定位置）
0x00006000                 E820 条目数组（最多128条 × 24B = 3KB）
0x00007000                 VBE 信息块（VBEInfo 512B）
0x00007200                 VBE 模式信息块（ModeInfo 256B）
0x00007C00                 Stage1 MBR（512B）
0x00008000                 AP 蹦床代码 trampoline（4KB，必须 < 1MB）
0x00009000                 AP 启动临时栈（每核 1KB，最多支持 32 核）
0x00010000                 Stage2 加载器（最多 64KB，实模式段 0x1000）
0x000A0000~0x000FFFFF      VGA/BIOS ROM 区（不可用）
0x00100000                 Kernel 镜像起始（链接地址 = 物理地址）
0x00300000                 BSS 区（由链接脚本控制）
0x00400000                 页表区：PML4@0x400000, PDPT@0x401000,
                                   PD[0]@0x402000, PD[1]@0x403000,
                                   PD[2]@0x404000, PD[3]@0x405000
0x00500000                 BSP 内核栈（栈顶 0x580000，512KB）
0x00580000                 SMP 控制块 + AP 计数器（原子变量）
0x10000000+                VESA LFB（地址由 VBE PhysBasePtr 决定）
```

**关键常量**（汇编 `.equ` 与 C `#define` 保持一致）：
```
BOOT_INFO_ADDR   = 0x5000
E820_BUFFER_ADDR = 0x6000
VBE_INFO_ADDR    = 0x7000
VBE_MODE_ADDR    = 0x7200
AP_TRAMPOLINE    = 0x8000   /* 4KB 对齐，SIPI vector = 0x08 */
AP_STACK_BASE    = 0x9000
PAGE_TABLE_BASE  = 0x400000
BSP_STACK_TOP    = 0x580000
SMP_INFO_ADDR    = 0x580000
```

---

## 项目文件结构

```
winixos/
├── boot/
│   ├── stage1.S            # MBR 引导扇区（≤512B）
│   └── stage2.S            # 第二阶段：E820/VBE/RSDP/模式切换
├── kernel/
│   ├── entry64.S           # BSP 64位入口，C运行时初始化，调用 kernel_main
│   ├── ap_trampoline.S     # AP 蹦床：16bit→32bit→64bit，链接到 0x8000
│   ├── main.c              # C 主函数：初始化 video/SMP，显示信息
│   ├── acpi.c / acpi.h     # ACPI 表解析：RSDP→RSDT/XSDT→MADT
│   ├── smp.c  / smp.h      # SMP 初始化：解析 MADT，写蹦床，发 INIT/SIPI
│   ├── video.c / video.h   # 帧缓冲绘制，字体渲染调用 font.h/font.c
│   └── util.c  / util.h    # itoa、memset、strlen（无 libc 依赖）
├── font.h                  # PSF2 字体头文件（已提供，直接使用）
└── font.c                  # PSF2 字体数据 + bitmap()/unicode()（已提供）
├── include/
│   ├── types.h             # 基础类型：uint8_t~uint64_t, uintptr_t
│   ├── boot_info.h         # BootInfo 结构体（汇编/C 共享）
│   └── e820.h              # E820Entry 结构体
├── linker/
│   ├── stage1.ld           # 链接到 0x7C00，binary 格式
│   ├── stage2.ld           # 链接到 0x10000，elf32-i386
│   └── kernel.ld           # 链接到 0x100000，elf64，导出 BSS 符号
├── Makefile
└── bochsrc.txt
```

---

## 详细实现步骤

### Step 1：`include/` 头文件

**`include/types.h`**
```c
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef uint64_t           uintptr_t;
```

**`include/boot_info.h`**（固定地址 0x5000，汇编按偏移写入，C 按结构体读取）
```c
typedef struct {
    uint32_t magic;          /* 0x00  魔数 0xB007B007 */
    uint32_t version;        /* 0x04  结构体版本 = 1 */
    uint64_t fb_addr;        /* 0x08  LFB 物理地址 */
    uint32_t fb_pitch;       /* 0x10  每行字节数（LinBytesPerScanLine）*/
    uint32_t fb_width;       /* 0x14  水平像素数 */
    uint32_t fb_height;      /* 0x18  垂直像素数 */
    uint8_t  fb_bpp;         /* 0x1C  色深（32）*/
    uint8_t  _pad[3];        /* 0x1D  对齐 */
    uint32_t e820_count;     /* 0x20  E820 条目数量 */
    uint32_t e820_addr;      /* 0x24  E820 数组物理地址（0x6000）*/
    uint64_t mem_total;      /* 0x28  可用内存字节总数 */
    uint64_t acpi_rsdp_addr; /* 0x30  RSDP 物理地址 */
    uint32_t boot_drive;     /* 0x38  引导驱动器号 */
    uint32_t cpu_count;      /* 0x3C  CPU 逻辑核心总数（含 BSP）*/
} __attribute__((packed)) BootInfo;
```

**`include/e820.h`**
```c
#define E820_USABLE   1
#define E820_RESERVED 2
typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_ext;
} __attribute__((packed)) E820Entry;
```

---

### Step 2：链接脚本

**`linker/stage1.ld`**
```ld
OUTPUT_FORMAT("binary")
ENTRY(_start)
SECTIONS {
    . = 0x7C00;
    .text : { *(.text) }
    . = 0x7C00 + 510;
    .sig : { SHORT(0xAA55) }
}
```

**`linker/stage2.ld`**
```ld
OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)
SECTIONS {
    . = 0x10000;
    .text   : { *(.text) *(.text.*) }
    .rodata : { *(.rodata) *(.rodata.*) }
    .data   : { *(.data) }
    .bss    : { *(.bss) *(COMMON) }
}
```
构建时用 `objcopy -O binary -j .text -j .rodata -j .data` 提取。

**`linker/kernel.ld`**
```ld
OUTPUT_FORMAT("elf64-x86-64")
ENTRY(kernel_entry)
SECTIONS {
    . = 0x100000;
    .text   : ALIGN(0x1000) { *(.text.entry) *(.text) *(.text.*) }
    .rodata : ALIGN(0x1000) { *(.rodata) *(.rodata.*) }
    .data   : ALIGN(0x1000) { *(.data) *(.data.*) }
    . = ALIGN(0x1000);
    __bss_start = .;
    .bss    : { *(.bss) *(.bss.*) *(COMMON) }
    __bss_end = .;
    . = ALIGN(0x1000);
    __kernel_end = .;
}
```

AP 蹦床使用单独链接脚本 `linker/trampoline.ld`，链接到 `0x8000`，输出 binary。

---

### Step 3：`boot/stage1.S` — MBR 引导扇区

约束：`.code16`，链接到 `0x7C00`，binary 输出后必须 = 512 字节。

**实现逻辑**：
1. `ljmp $0x0000, $_start` — 规范化 CS=0, IP=正确偏移
2. 设置 `DS=ES=SS=0`，`SP=0x7BFF`
3. 保存 `DL`（引导驱动器号）到约定内存地址（`0x5038` = BootInfo.boot_drive）
4. **开 A20（Fast A20）**：
   ```
   inb  $0x92, %al
   orb  $0x02, %al
   andb $0xFE, %al   /* 不要置 bit0（会触发 reset）*/
   outb %al, $0x92
   ```
5. **通过 INT 13h/AH=42h（扩展读）加载 Stage2** 到 `0x1000:0x0000`：
   - DAP：`{0x10, 0, STAGE2_SECTORS, 0x0000, 0x1000, LBA=1, 0}`
   - `DL` = 驱动器号，失败显示 `'S'` 后 hlt
6. `ljmp $0x1000, $0x0000` — 跳转到 Stage2

错误处理子函数：`INT 10h/AH=0x0E` 打印单个字符。
尾部通过链接脚本填充到 510 字节后追加 `0xAA55`。

**Makefile 中先构建 stage2.bin，计算扇区数后通过 `--defsym STAGE2_SECTORS=N` 传入 stage1.S 编译**。

---

### Step 4：`boot/stage2.S` — 第二阶段加载器

文件包含三段代码，通过 `.code16`/`.code32` 切换。

#### 4a. 实模式段（.code16 @ 0x10000）

**段寄存器设置**：`DS=ES=FS=GS=0x1000`（Stage2 在段 0x1000），`SS=0x0000, SP=0x7BFF`

**E820 内存探测**：
```
ES=0x0000, DI=0x6000   /* 目标缓冲区：低内存 0x6000 */
EBX=0（首次调用）
循环：
  EAX=0xE820, ECX=24, EDX=0x534D4150
  INT 0x15
  检查 CF=0 且 EAX=0x534D4150
  将条目写入 0x6000 + count*24
  count++
  若 EBX=0 则结束
将 count → BootInfo+0x20
统计 type=1 的 length 累加 → BootInfo+0x28
```

**VBE 图形模式（三轮扫描回退）**：
```
分辨率优先级：1680×1050 → 1440×900 → 800×600

AX=0x4F00, ES=0, DI=0x7000, INT 0x10   /* 获取 VBEInfo */
检查返回 AX==0x004F，签名 "VESA"

遍历 VideoModePtr 指向的模式列表（以 0xFFFF 结尾）：
  对每个模式号：
    AX=0x4F01, CX=mode_num, ES=0, DI=0x7200, INT 0x10
    读取 ModeInfoBlock：
      偏移 0x00: ModeAttributes（bit0=硬件支持，bit7=LFB可用）
      偏移 0x12: XResolution（word）
      偏移 0x14: YResolution（word）
      偏移 0x19: BitsPerPixel（byte）
      偏移 0x1B: MemoryModel（byte，6=直接颜色）
      偏移 0x28: PhysBasePtr（dword）★ LFB 物理地址
      偏移 0x31: LinBytesPerScanLine（word）★ 每行字节数
    三轮扫描分别记录匹配的模式号

选定模式后：
  AX=0x4F02, BX=mode_num|0x4000, INT 0x10  /* bit14=线性帧缓冲 */
  PhysBasePtr → BootInfo.fb_addr
  LinBytesPerScanLine → BootInfo.fb_pitch
  XRes/YRes/BPP → BootInfo 对应字段
```

**ACPI RSDP 探测**：
```
/* 搜索 1：EBDA 区域 */
读 [0x040E] 得到 EBDA 段地址 → 物理地址 = seg << 4
在 [EBDA_start, EBDA_start+1KB] 范围内每 16 字节检查签名 "RSD PTR "

/* 搜索 2：BIOS ROM 区 0xE0000~0xFFFFF */
每 16 字节检查签名 "RSD PTR "
验证 checksum（前 20 字节之和 mod 256 = 0）

找到后将物理地址 → BootInfo.acpi_rsdp_addr
```

**最终写入 BootInfo magic** = `0xB007B007`

**GDT 构建**（在 Stage2 数据段中定义 `.align 8` 的标签）：
```
NULL        : 8字节全零
代码段32    : Base=0, Limit=0xFFFFF, G=1, D/B=1, P=1, DPL=0, Type=0xA → 选择子 0x08
数据段32    : Base=0, Limit=0xFFFFF, G=1, D/B=1, P=1, DPL=0, Type=0x2 → 选择子 0x10
代码段64    : Base=0, Limit=0,      G=1, L=1, D=0, P=1, DPL=0, Type=0xA → 选择子 0x18
数据段64    : Base=0, Limit=0xFFFFF, G=1, D/B=1, P=1, DPL=0, Type=0x2 → 选择子 0x20
```

**进入保护模式**：
```asm
cli
lgdt gdtr        /* data32 lgdt 确保 32 位操作数 */
movl %cr0, %eax
orl  $0x1, %eax
movl %eax, %cr0
ljmp $0x08, $pm32_entry   /* 刷新 CS，进入 32 位代码段 */
```

#### 4b. 32 位保护模式段（.code32）

```asm
pm32_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl $0x7BFF, %esp
```

**CPUID 检测长模式支持**：
```asm
movl $0x80000000, %eax
cpuid
cmpl $0x80000001, %eax
jb   no_longmode         /* 不支持扩展叶 */
movl $0x80000001, %eax
cpuid
testl $(1<<29), %edx     /* bit29 = Long Mode 支持 */
jz   no_longmode
```

**构建 4 级页表（Identity Mapping 前 4GB，2MB 大页）**：
```asm
/* 清零 6 个页面（PML4 + PDPT + 4×PD） */
movl $0x400000, %edi
movl $0x406000, %ecx
subl %edi, %ecx
shrl $2, %ecx
xorl %eax, %eax
rep stosl

/* PML4[0] → PDPT */
movl $(0x401000 | 0x3), %eax
movl %eax, (0x400000)

/* PDPT[0..3] → PD[0..3] */
movl $(0x402000 | 0x3), %eax
movl %eax, (0x401000)
movl $(0x403000 | 0x3), %eax
movl %eax, (0x401008)
movl $(0x404000 | 0x3), %eax
movl %eax, (0x401010)
movl $(0x405000 | 0x3), %eax
movl %eax, (0x401018)

/* 填充 PD[k][j]：每个条目 = 物理地址 | 0x83（PS=1 大页） */
/* k=0..3, j=0..511 → 地址 = k*1GB + j*2MB */
```

**进入 64 位长模式**：
```asm
/* 1. CR4.PAE = 1 */
movl %cr4, %eax
orl  $(1<<5), %eax
movl %eax, %cr4

/* 2. CR3 = PML4 地址 */
movl $0x400000, %eax
movl %eax, %cr3

/* 3. IA32_EFER.LME = 1 */
movl $0xC0000080, %ecx
rdmsr
orl  $(1<<8), %eax
wrmsr

/* 4. CR0.PG = 1（同时保持 PE=1）*/
movl %cr0, %eax
orl  $0x80000001, %eax
movl %eax, %cr0

/* 5. 远跳转到 64 位代码段（通过内存中的 FAR 指针）*/
movl $0x100000, jump64_target     /* kernel_entry 地址 */
movw $0x18,     jump64_target+4   /* 64 位代码段选择子 */
ljmpl *(jump64_target)
```

---

### Step 5：`kernel/entry64.S` — BSP 64 位 C 运行时初始化

```asm
.section .text.entry
.code64
.global kernel_entry
kernel_entry:
    /* 更新数据段寄存器 */
    movw $0x20, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax,  %ax
    movw %ax,  %fs
    movw %ax,  %gs

    /* 建立 BSP 内核栈 */
    movq $0x580000, %rsp
    andq $~0xF, %rsp    /* 16 字节对齐 */

    /* 清零 BSS */
    movq $__bss_start, %rdi
    movq $__bss_end,   %rcx
    subq %rdi, %rcx
    shrq $3,   %rcx
    xorq %rax, %rax
    rep stosq

    /* 调用 C 主函数，传入 BootInfo 指针 */
    movq $0x5000, %rdi
    call kernel_main

.Lhalt:
    cli
    hlt
    jmp .Lhalt
```

---

### Step 6：`kernel/ap_trampoline.S` — AP 蹦床代码

AP 收到 SIPI 后从 `0x8000` 开始以 16 位实模式执行。蹦床必须完全自包含（不依赖外部符号），链接地址 = `0x8000`。

```
ap_trampoline.S 结构（链接到 0x8000）：

[.code16]
ap_start:
  cli / cld
  xorw %ax, %ax
  movw %ax, %ds / %es / %ss
  movl $0x8FFE, %esp          /* 临时栈，蹦床内部 */

  /* 加载 BSP 阶段已建好的 GDT（地址硬编码，Stage2 在固定地址写了 GDT） */
  lgdt (gdt_ptr_addr)          /* gdt_ptr_addr = Stage2 GDT 描述符地址 */

  /* 进入保护模式 */
  movl %cr0, %eax
  orl  $1, %eax
  movl %eax, %cr0
  ljmp $0x08, $ap_pm32         /* 段内偏移（相对 0x8000）*/

[.code32]
ap_pm32:
  /* 重置段寄存器 */
  movw $0x10, %ax; ...

  /* 复用 BSP 已建好的页表（CR3 = 0x400000）*/
  movl $0x400000, %eax
  movl %eax, %cr3

  /* CR4.PAE = 1 */
  movl %cr4, %eax; orl $(1<<5), %eax; movl %eax, %cr4

  /* EFER.LME = 1 */
  movl $0xC0000080, %ecx; rdmsr; orl $(1<<8), %eax; wrmsr

  /* CR0.PG = 1 */
  movl %cr0, %eax; orl $0x80000001, %eax; movl %eax, %cr0

  /* 远跳 64 位代码段，目标 = ap_entry64（地址硬编码于蹦床数据区）*/
  ljmpl *(ap_jump64)

[.code64]
ap_entry64:
  /* 更新段寄存器 */
  movw $0x20, %ax; ...

  /* 获取本核 ID（LAPIC ID 寄存器）*/
  movq $0xFEE00020, %rax     /* LAPIC_ID 寄存器地址 */
  movl (%rax), %eax
  shrl $24, %eax              /* LAPIC ID 在 bit31:24 */

  /* 分配独立栈：AP_STACK_BASE + lapic_id * 4096 */
  movq $0x9000, %rsp
  shlq $12, %rax
  addq %rax, %rsp
  addq $0x1000, %rsp          /* 指向该核栈顶 */

  /* 原子递增 cpu_count */
  lock incl (0x57FC)           /* SMP_INFO_ADDR 处的计数器 */

  /* 进入等待循环（将来由调度器替换）*/
ap_idle:
  hlt
  jmp ap_idle
```

---

### Step 7：`kernel/acpi.c` — ACPI 表解析

**函数**：
- `uint8_t acpi_checksum(void *ptr, uint32_t len)` — 计算校验和
- `void *acpi_find_table(const char *sig)` — 在 RSDT/XSDT 中查找指定签名表
  - 从 `BootInfo.acpi_rsdp_addr` 读取 RSDP
  - 检查 RSDP 版本（revision ≥ 2 时优先用 XSDT，否则用 RSDT）
  - 遍历 RSDT/XSDT 中的所有表指针，对每个表头比较 4 字节签名

**关键结构体**（`acpi.h`）：
```c
typedef struct { char sig[8]; uint8_t cksum; ... uint32_t rsdt_addr;
                 uint32_t len; uint64_t xsdt_addr; ... } __packed RSDP;
typedef struct { char sig[4]; uint32_t len; uint8_t rev; uint8_t cksum;
                 char oem[6]; ... } __packed ACPIHeader;
typedef struct { ACPIHeader hdr; /* 后跟 uint32_t 或 uint64_t 数组 */ } __packed RSDT;
```

---

### Step 8：`kernel/smp.c` — SMP 多核初始化

**MADT 结构**（ACPI MADT = Multiple APIC Description Table，签名 `"APIC"`）：
```c
typedef struct { ACPIHeader hdr; uint32_t lapic_addr; uint32_t flags; } __packed MADT;
/* 后跟可变长度的条目序列，每条目格式：{type(1B), len(1B), ...} */
/* type=0: Processor Local APIC → 含 apic_id, flags.enabled */
/* type=1: I/O APIC */
/* type=5: Local APIC Address Override（64位地址，覆盖 MADT.lapic_addr）*/
```

**`smp_init(BootInfo *info)` 实现流程**：

```
1. 从 ACPI 找到 MADT 表
2. 读取 LAPIC 物理基地址（先用 MADT.lapic_addr，若有 type=5 条目则用覆盖值）
   - 也可从 MSR IA32_APIC_BASE (0x1B) 读取：bits[35:12] << 12
3. 遍历 MADT 条目，收集所有 type=0（Processor Local APIC）条目
   - 跳过 flags.enabled=0 的条目（禁用的核）
   - 跳过与 BSP 相同 LAPIC ID 的条目
   - 记录所有 AP 的 LAPIC ID 到数组，统计 ap_count
4. 将 BootInfo.cpu_count = 1（BSP）+ ap_count

5. 将 ap_trampoline.bin 内容复制到物理地址 0x8000
   （trampoline 在 kernel 镜像中以 .section .trampoline 形式嵌入，
    通过链接脚本导出 __trampoline_start/__trampoline_end 符号）

6. 对每个 AP 的 LAPIC ID 执行 INIT-SIPI-SIPI 序列：
   a. 发送 INIT IPI：
      LAPIC ICR_HIGH[31:24] = target_apic_id
      LAPIC ICR_LOW = 0x000C4500  （INIT，Level=Assert，Dest=物理）
      等待 ICR_LOW bit12（Send Pending）清零
      等待 10ms

   b. 发送 SIPI #1：
      LAPIC ICR_HIGH[31:24] = target_apic_id
      LAPIC ICR_LOW = 0x000C4600 | (AP_TRAMPOLINE >> 12)
                    = 0x000C4608  （SIPI，vector=0x08 → 物理地址 0x8000）
      等待 ICR 清零
      等待 200μs

   c. 发送 SIPI #2（重复，提高可靠性）：
      同上
      等待 200μs

7. 等待最多 200ms，轮询 cpu_count 达到预期值
8. 返回实际已启动的核心数
```

**LAPIC 寄存器访问**（MMIO，基地址通常 `0xFEE00000`）：
```c
#define LAPIC_ID        0x020  /* Local APIC ID */
#define LAPIC_EOI       0x0B0
#define LAPIC_ICR_LOW   0x300  /* Interrupt Command Register 低32位 */
#define LAPIC_ICR_HIGH  0x310  /* Interrupt Command Register 高32位 */

static inline uint32_t lapic_read(uint64_t base, uint32_t reg) {
    return *((volatile uint32_t *)(base + reg));
}
static inline void lapic_write(uint64_t base, uint32_t reg, uint32_t val) {
    *((volatile uint32_t *)(base + reg)) = val;
}
```

**延时实现**（无 PIT/HPET 的简单忙等）：
```c
static void udelay(uint32_t us) {
    /* 使用 RDTSC 估算，或简单循环（需 calibrate）*/
    /* 最简方案：固定循环次数（QEMU/真实硬件近似值）*/
    volatile uint64_t n = us * 1000ULL;
    while (n--) __asm__("pause");
}
```

---

### Step 9：`kernel/video.c` — 帧缓冲图形输出

**字体来源**：使用项目根目录已提供的 `font.h` / `font.c`（PSF2 格式，ZAP Light 字体）。

PSF2 字体接口：
- `__font_initialize__()` — 初始化字体，解析 Unicode 映射表，须在使用前调用一次
- `bitmap(char c)` — 返回字符 `c` 对应字形位图首地址（`unsigned short *`）
- `font->width`、`font->height` — 字形宽高（约 10px × 20px）
- 每行 1 个 `unsigned short`（2字节），**bit15 为最左像素**（高位在左）

**全局状态**（模块级静态变量）：
```c
static volatile uint32_t *fb_base;
static uint32_t fb_width, fb_height, fb_pitch;
static int cursor_x, cursor_y;
```

**核心接口**：
```c
void video_init(uint64_t addr, uint32_t w, uint32_t h, uint32_t pitch, uint8_t bpp);
void video_clear(uint32_t color);
void video_put_pixel(int x, int y, uint32_t rgb);
void video_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void video_print(const char *s, uint32_t fg);   /* 支持 '\n'，自动换行 */
```

**`video_init`** 内部调用 `__font_initialize__()` 完成字体初始化。

**`video_draw_char` 实现**（利用 PSF2 bitmap 接口）：
```c
void video_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    unsigned short *glyph = bitmap(c);
    for (uint32_t row = 0; row < font->height; row++) {
        unsigned short line = glyph[row];
        for (uint32_t col = 0; col < font->width; col++) {
            /* bit15 为最左像素，右移 (15-col) 取当前列的像素位 */
            uint32_t color = (line >> (15 - col)) & 1 ? fg : bg;
            video_put_pixel(x + col, y + row, color);
        }
    }
}
```

**像素写入**：
```c
static inline void video_put_pixel(int x, int y, uint32_t rgb) {
    fb_base[y * (fb_pitch / 4) + x] = rgb;   /* 32bpp B8G8R8X8 */
}
```

**`video_print`**：维护 `cursor_x/cursor_y`，每字符步进 `font->width`，每行步进 `font->height`，遇 `'\n'` 换行，到屏幕底部时回绕到顶部。

**颜色常量**：
```c
#define COLOR_BG     0x00001428   /* 深蓝背景 */
#define COLOR_WHITE  0x00FFFFFF
#define COLOR_CYAN   0x0000FFFF
#define COLOR_GREEN  0x0000FF80
#define COLOR_YELLOW 0x00FFFF00
#define COLOR_RED    0x00FF4040
```

---

### Step 10：`kernel/main.c` — C 主函数

```c
void kernel_main(BootInfo *info) {
    /* 1. 校验 magic */
    if (info->magic != 0xB007B007) { /* 死循环 */ }

    /* 2. 初始化视频 */
    video_init(info->fb_addr, info->fb_width, info->fb_height,
               info->fb_pitch, info->fb_bpp);
    video_clear(COLOR_BG);

    /* 3. 显示 Banner */
    video_print("WinixOS Bootloader v1.0\n", COLOR_CYAN);
    video_print("========================\n", COLOR_WHITE);

    /* 4. 显示分辨率 */
    /* "Display: 1680x1050x32\n" */

    /* 5. 统计并显示内存 */
    E820Entry *e820 = (E820Entry *)(uintptr_t)info->e820_addr;
    uint64_t usable = 0;
    for (uint32_t i = 0; i < info->e820_count; i++)
        if (e820[i].type == E820_USABLE) usable += e820[i].length;
    /* "Total Usable Memory: XXXX MB\n" */

    /* 6. 显示 E820 表 */
    /* 逐行打印 base/len/type */

    /* 7. SMP 初始化 */
    video_print("\nInitializing SMP...\n", COLOR_YELLOW);
    int cpus = smp_init(info);
    /* "CPU Cores Online: N\n" */

    /* 8. 永久 HLT */
    while (1) __asm__("hlt");
}
```

---

### Step 11：`Makefile` — 构建系统

**工具链**（MSYS2 环境中，使用系统原生工具）：
```makefile
AS      := as
CC      := gcc
LD      := ld
OBJCOPY := objcopy

# Stage1/Stage2 目标：elf32-i386
AS16    := $(AS) --32
LD32    := $(LD) -melf_i386

# Kernel 目标：elf64-x86-64
AS64    := $(AS) --64
LD64    := $(LD) -melf_x86_64

CFLAGS  := -m64 -std=c11 -ffreestanding -fno-stack-protector \
           -fno-pic -mno-red-zone -nostdlib -nostdinc \
           -I include -I . -O2 -Wall -Wextra
```

注意：`-I .` 使 `font.h` 可从项目根目录直接引用（`#include <font.h>`）。

**构建目标依赖图**：
```
stage2.bin（先构建）
  → 计算 STAGE2_SECTORS
  → stage1.bin（依赖 STAGE2_SECTORS）

kernel.bin（独立构建）
  → ap_trampoline.bin（链接到 0x8000）嵌入 kernel ELF

os.img：
  sector 0              : stage1.bin (512B)
  sector 1..N           : stage2.bin
  sector N+1..M         : kernel.bin
```

**镜像打包**：
```makefile
os.img: stage1.bin stage2.bin kernel.bin
    dd if=/dev/zero of=$@ bs=512 count=8192
    dd if=stage1.bin of=$@ bs=512 conv=notrunc
    dd if=stage2.bin of=$@ bs=512 seek=1 conv=notrunc
    dd if=kernel.bin of=$@ bs=512 seek=$(KERNEL_LBA) conv=notrunc
```

**QEMU 运行**：
```makefile
QEMU_ARGS := -drive file=os.img,format=raw,index=0,media=disk \
             -m 256M -smp 4 -vga std -display sdl \
             -no-reboot -no-shutdown

run: os.img
    qemu-system-x86_64 $(QEMU_ARGS)

debug: os.img
    qemu-system-x86_64 $(QEMU_ARGS) -s -S
```
注意：`-smp 4` 启动 4 核，用于测试 SMP 初始化。

**Bochs 配置** `bochsrc.txt`：
```
megs: 256
cpuid: ncores=4
ata0-master: type=disk, path=os.img, mode=flat
boot: disk
display_library: sdl2
```

---

## 关键技术坑点

| # | 坑点 | 解决方案 |
|---|------|----------|
| 1 | Stage1 必须恰好 512B | 链接脚本强制 `. = 0x7C00+510`，追加 `0xAA55` |
| 2 | A20 开启方式 | 使用 Fast A20 (`0x92` 端口)，bit1=1，**注意 bit0 不能置 1**（会触发复位） |
| 3 | E820 首次 EBX=0 | 严格初始化，不复用上次返回值 |
| 4 | VBE 必须实模式调用 | 所有 VBE INT 10h 在 Stage2 实模式段完成 |
| 5 | VBE LFB bit14 | `BX = mode_num \| 0x4000` 才能启用线性帧缓冲 |
| 6 | LinBytesPerScanLine | 使用 ModeInfo 偏移 `0x31`（非 `0x10`），含对齐填充 |
| 7 | EFER MSR 写法 | 先 RDMSR 读取当前值，OR bit8，再 WRMSR 写回 |
| 8 | 64 位远跳转 | 32 位保护模式下通过 `ljmpl *(mem)` 结构体方式跳入 64 位 |
| 9 | `-mno-red-zone` | C 内核代码必须加，中断/异常会破坏栈下方 128B |
| 10 | BSS 清零 | `entry64.S` 用 `__bss_start/__bss_end` + `REP STOSQ` |
| 11 | 栈 16B 对齐 | RSP 设置为 16B 对齐值，CALL 压 8B 后满足 ABI（`RSP ≡ 8 mod 16`）|
| 12 | AP 蹦床地址 < 1MB | SIPI vector 字段只有 8 位，物理地址 = vector × 4096，最大 0xFF000 |
| 13 | INIT-SIPI-SIPI 时序 | INIT 后等 10ms，每个 SIPI 后等 200μs，规范要求两次 SIPI |
| 14 | AP 栈隔离 | 每个 AP 用独立 4KB 栈（按 LAPIC ID 偏移），避免栈重叠 |
| 15 | AP 核心计数原子性 | 使用 `lock incl` 保证多核同时递增时的原子性 |
| 16 | ACPI XSDT vs RSDT | RSDP revision≥2 时用 XSDT（8字节指针），否则 RSDT（4字节指针） |

---

## 文件创建顺序

```
1.  include/types.h
2.  include/e820.h
3.  include/boot_info.h
4.  linker/stage1.ld
5.  linker/stage2.ld
6.  linker/kernel.ld
7.  linker/trampoline.ld
8.  boot/stage1.S
9.  boot/stage2.S
10. kernel/entry64.S
11. kernel/ap_trampoline.S
12. kernel/video.h + kernel/video.c   （使用 font.h/font.c PSF2 字体接口）
13. kernel/util.h  + kernel/util.c    （itoa/memset/strlen，无 libc 依赖）
14. kernel/acpi.h  + kernel/acpi.c
15. kernel/smp.h   + kernel/smp.c
16. kernel/main.c
17. Makefile                           （font.c 与 kernel 其他 .c 一起编译）
18. bochsrc.txt
```

注意：`font.h` 和 `font.c` 已存在于项目根目录，**不需要创建**，直接参与编译即可。

---

## 验证方法

```bash
# 构建全部
make all

# 验证 Stage1 大小
wc -c build/stage1.bin    # 必须 = 512

# 验证 Stage1 签名（最后2字节应为 55 aa）
xxd build/stage1.bin | tail -1

# 运行（单核）
make run

# 运行（4核，测试 SMP）
qemu-system-x86_64 -drive file=build/os.img,format=raw \
    -m 256M -smp 4 -vga std -display sdl -no-reboot

# GDB 调试（Stage1 实模式断点）
make debug   # 另开终端：
gdb -ex "target remote :1234" \
    -ex "set architecture i8086" \
    -ex "break *0x7c00" \
    -ex "continue"

# 预期输出
# ┌──────────────────────────────────────────┐
# │  WinixOS Bootloader v1.0                │
# │  ========================               │
# │  Display: 1680x1050x32                  │
# │  Total Usable Memory: 253 MB            │
# │                                         │
# │  E820 Memory Map:                       │
# │  [0x0000000000000000 + 0x9F000] type=1  │
# │  [0x0000000000100000 + 0xEE00000] type=1│
# │  ...                                    │
# │                                         │
# │  Initializing SMP...                    │
# │  CPU Cores Online: 4                    │
# └──────────────────────────────────────────┘
```
