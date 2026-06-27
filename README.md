# WinixOS

A Unix-like 64-bit hobby operating system with SMP support, implemented in C and x86-64 assembly.

## Overview

WinixOS is a lightweight educational operating system that demonstrates core OS concepts including:

- **Multi-boot Support**: Custom bootloader with MBR + Stage2 loader
- **64-bit Long Mode**: Full x86-64 architecture support
- **SMP (Symmetric Multi-Processing)**: Multi-core CPU initialization via ACPI/APIC
- **Interrupt Handling**: Complete IDT setup with exception and IRQ handlers
- **Physical Memory Management**: E820-based memory detection and page allocator
- **Graphics Framebuffer**: VESA VBE mode setting with 2D graphics primitives
- **Custom libc**: Minimal C standard library implementation

## Directory Structure

```
winxvos/
├── boot/                    # Bootloader (16/32-bit assembly)
│   ├── boot.S              # MBR boot sector (512 bytes)
│   ├── setup.S             # Stage2 loader (real → protected → long mode)
│   ├── boot.ld             # Linker script for boot sector
│   └── setup.ld            # Linker script for Stage2
├── kernel/                  # Kernel source code
│   ├── main.c              # Kernel main entry point
│   ├── entry64.S           # 64-bit entry point
│   ├── trap_entry.S        # Interrupt/exception handlers
│   ├── ap_trampoline.S     # AP startup code for SMP
│   ├── idt.c               # Interrupt Descriptor Table setup
│   ├── kalloc.c            # Physical page allocator
│   ├── spinlock.c          # Spinlock implementation
│   ├── cpu.c               # CPU identification
│   ├── smp.c               # SMP initialization
│   ├── acpi.c              # ACPI table parsing
│   ├── lapic.c             # Local APIC operations
│   ├── pic.c               # 8259A PIC remapping
│   ├── video.c             # Framebuffer graphics driver
│   ├── font.c              # PSF2 font rendering
│   ├── panic.c             # Kernel panic handler
│   ├── message.c           # Boot banner display
│   ├── winxvos.c           # OS-specific utilities
│   ├── defs.h              # Function declarations
│   ├── bootinfo.h          # Boot information structure
│   ├── e820.h              # E820 memory map definitions
│   ├── memlayout.h         # Memory layout constants
│   ├── param.h             # System parameters
│   ├── x86_64.h            # x86-64 specific instructions
│   ├── idt.h               # IDT structures
│   ├── acpi.h              # ACPI table structures
│   ├── lapic.h             # LAPIC register definitions
│   ├── pic.h               # PIC port definitions
│   ├── proc.h              # Process structures (future)
│   ├── kalloc.h            # Kernel allocator interface
│   ├── spinlock.h          # Spinlock interface
│   ├── smp.h               # SMP interface
│   ├── video.h             # Video/Graphics API
│   ├── font.h              # Font structures
│   └── message.h           # Banner text
├── libc/                    # Minimal C standard library
│   ├── stdio.c             # printf, sprintf, etc.
│   ├── string.c            # memset, memcpy, strlen, etc.
│   ├── ctype.c             # Character type functions
│   ├── assert.c            # Assertion handling
│   ├── stdio.h             # Stdio declarations
│   ├── string.h            # String function declarations
│   ├── ctype.h             # Character type declarations
│   ├── stdarg.h            # Variable argument macros
│   ├── types.h             # Basic type definitions
│   └── libc.h              # Master include file
├── linker/                  # Linker scripts
│   ├── kernel.ld           # Kernel ELF layout
│   └── trampoline.ld       # AP trampoline binary layout
├── tools/                   # Build tools
│   └── gen_irq_vectors.py  # Generate IRQ handler stubs
├── Makefile                 # Build system
├── bochsrc.txt             # Bochs emulator configuration
├── LICENSE                  # License file
└── README.md                # This file
```

## Build Requirements

### Toolchain
- **Clang** (with x86_64 and i386 targets)
- **LLD** (ELF linker)
- **LLVM objcopy**
- **Python 3** (for IRQ vector generation)

### Emulator (for testing)
- **QEMU** (x86_64 system emulation)
  - Or **Bochs** (configuration provided in `bochsrc.txt`)

### Building on Linux/WSL

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install clang llvm python3 qemu-system-x86

# Build the OS image
make

# Run in QEMU
make run

# Debug mode (with GDB server)
make debug
```

### Building on Windows (MSYS2)

```bash
# Install MSYS2 toolchain
pacman -S mingw-w64-x86_64-clang mingw-w64-x86_64-qemu python3

# The Makefile is configured for MSYS2 paths
make
```

## Boot Process

### Stage 1: MBR Boot Sector (`boot/boot.S`)
- Loaded by BIOS at `0x7C00`
- Enables A20 address line
- Loads Stage2 via INT 13h LBA
- Exactly 512 bytes with boot signature

### Stage 2: Setup Loader (`boot/setup.S`)
- Runs in real mode initially
- Detects memory map via E820 BIOS call
- Sets VESA VBE graphics mode
- Searches ACPI RSDP tables
- Builds boot_info structure at `0x5000`
- Creates GDT and enters protected mode
- Loads kernel via ATA PIO
- Sets up 4-level paging (identity mapped)
- Enters long mode and jumps to kernel

### Kernel Entry (`kernel/entry64.S`)
- Sets up segment registers
- Enables SSE/SSE2
- Initializes BSP kernel stack
- Clears BSS section
- Calls `kernel_main()`

## Kernel Features

### Memory Management
- **E820 Detection**: Parses BIOS memory map
- **Page Allocator**: xv6-style free list
- **4KB Pages**: Standard x86-64 page size
- **Spinlock Protection**: Thread-safe allocation

### Interrupt Handling
- **IDT**: 256 entries for exceptions and IRQs
- **Exception Handlers**: All CPU exceptions (0-19)
- **IRQ Handlers**: Auto-generated for vectors 32-255
- **Trap Frame**: Complete register save/restore

### SMP Support
- **ACPI Parsing**: MADT table for CPU discovery
- **AP Wakeup**: INIT-SIPI-SIPI sequence
- **LAPIC**: Local APIC timer and IPI
- **CPU Count**: Runtime multi-core detection

### Graphics
- **VESA VBE**: Mode detection and setting
- **PSF2 Fonts**: High-quality console fonts
- **2D Primitives**:
  - Lines (Bresenham algorithm)
  - Rectangles (filled and outline)
  - Circles (Midpoint algorithm)
  - Rounded rectangles
  - Polygons (scanline fill)

## Key Data Structures

### zenith_boot_info
```c
typedef struct {
    uint32_t magic;           // 0xB007B007
    uint32_t version;
    uint64_t fb_addr;         // Framebuffer address
    uint32_t fb_pitch;
    uint32_t fb_width;
    uint32_t fb_height;
    uint8_t  fb_bpp;
    uint32_t e820_count;
    uint32_t e820_addr;
    uint64_t acpi_rsdp_addr;
    uint32_t cpu_count;
} zenith_boot_info;
```

### trapframe
```c
struct trapframe {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t trapno;
    uint64_t err;
    uint64_t rip, cs, rflags, rsp, ss;
};
```

## Memory Layout

| Region | Address | Description |
|--------|---------|-------------|
| MBR | 0x7C00 | Boot sector |
| Stage2 | 0x10000 | Second stage loader |
| Boot Info | 0x5000 | Boot information structure |
| E820 Buffer | 0x6000 | Memory map data |
| Kernel | 0x100000 | Kernel image |
| AP Trampoline | 0x8000 | AP startup code |
| Kernel Stack (BSP) | 0x580000 | Bootstrap processor stack |

## License

See [LICENSE](LICENSE) for details.

## Acknowledgments

This project draws inspiration from:
- **xv6**: Educational Unix-like OS from MIT
- **Linux**: Various implementation patterns
- **Intel SDM**: Architecture reference manual
- **OSDev Wiki**: Community knowledge base

## Status

WinixOS is a work in progress. Current features:
- ✅ Multi-stage boot process
- ✅ 64-bit long mode
- ✅ SMP initialization
- ✅ Interrupt handling
- ✅ Physical memory allocation
- ✅ Graphics framebuffer
- 🚧 Process scheduling (planned)
- 🚧 Virtual memory (planned)
- 🚧 Filesystem (planned)