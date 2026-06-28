# WinixOS Makefile
# Toolchain: Clang (ELF output) + LLD (ELF linker)
# 架构师注: 针对高半核 (Higher Half) 与 Freestanding 环境进行了深度优化

CLANG   := /usr/bin/clang
LLD     := /usr/bin/ld.lld
OBJCOPY := /usr/bin/llvm-objcopy
QEMU    := /mingw64/bin/qemu-system-x86_64

BUILD   := build
BOOT    := boot
KERNEL  := kernel
LIBC    := libc

# ============================================================================
# 编译与汇编标志 (核心架构配置)
# ============================================================================

# 16位/32位实模式/保护模式汇编标志 (用于 Bootloader)
AS16FLAGS := -target i386-unknown-linux-gnu -ffreestanding -nostdinc -c

# 64位长模式汇编标志
# 架构师修复: 必须添加 -mcmodel=large，否则汇编中的绝对地址引用 (如 movq $sym, %rax) 
# 会在链接时触发与 C 代码相同的 R_X86_64_32S 溢出错误。
AS64FLAGS := -target x86_64-unknown-linux-gnu -ffreestanding -nostdinc -mcmodel=large -c

# 64位 C 语言内核编译标志
CFLAGS := -target x86_64-unknown-linux-gnu \
          -m64 -std=c11 \
          -ffreestanding -fno-stack-protector \
          -mcmodel=large -fno-pic -fno-pie \
          -mno-red-zone \
          -mno-sse -mno-sse2 -mno-mmx -mno-avx -mno-80387 \
          -nostdlib -nostdinc \
          -I libc -I . -I kernel \
          -I kernel/arch/x86_64 \
          -I kernel/drivers/video \
          -I kernel/drivers/fs \
          -O2 -Wall -Wextra -Wno-unused-parameter \
          -c

# ============================================================================
# 源文件定义
# ============================================================================

# Kernel C sources
KERNEL_C_SRCS := \
    $(KERNEL)/main.c     \
    $(KERNEL)/arch/x86_64/cpu.c      \
    $(KERNEL)/arch/x86_64/acpi.c     \
    $(KERNEL)/arch/x86_64/smp.c      \
    $(KERNEL)/arch/x86_64/kalloc.c   \
    $(KERNEL)/arch/x86_64/spinlock.c \
    $(KERNEL)/arch/x86_64/idt.c      \
    $(KERNEL)/arch/x86_64/pic.c      \
    $(KERNEL)/arch/x86_64/lapic.c    \
    $(KERNEL)/drivers/video/video.c  \
    $(KERNEL)/drivers/video/font.c   \
    $(KERNEL)/panic.c    \
    $(KERNEL)/message.c   \
    $(KERNEL)/winxvos.c   \
    $(LIBC)/string.c      \
    $(LIBC)/ctype.c       \
    $(LIBC)/stdio.c       \
    $(LIBC)/assert.c

# Kernel assembly sources
KERNEL_S_SRCS := \
    $(KERNEL)/arch/x86_64/entry64.S      \
    $(KERNEL)/arch/x86_64/trap_entry.S   \
    $(KERNEL)/arch/x86_64/irq_vectors.S

KERNEL_C_OBJS := $(patsubst %.c, $(BUILD)/%.o, $(KERNEL_C_SRCS))
KERNEL_S_OBJS := $(patsubst %.S, $(BUILD)/%.o, $(KERNEL_S_SRCS))
TRAMP_OBJ     := $(BUILD)/$(KERNEL)/arch/x86_64/ap_trampoline_raw.o

.PHONY: all clean run debug dirs gen-irq

all: dirs gen-irq $(BUILD)/os.img

dirs:
	mkdir -p $(BUILD)/$(BOOT) $(BUILD)/$(KERNEL)/arch/x86_64 $(BUILD)/$(KERNEL)/drivers/video $(BUILD)/$(KERNEL)/drivers/fs $(BUILD)/$(LIBC)

# Generate IRQ handler stubs from script
gen-irq:
	python3 tools/gen_irq_vectors.py $(KERNEL)/arch/x86_64/irq_vectors.S || python tools/gen_irq_vectors.py $(KERNEL)/arch/x86_64/irq_vectors.S

# ============================================================================
# AP Trampoline 构建 (16位 -> 32位 ELF -> 纯二进制 -> 64位 ELF 对象)
# ============================================================================

$(BUILD)/$(KERNEL)/arch/x86_64/ap_trampoline.o: $(KERNEL)/arch/x86_64/ap_trampoline.S
	$(CLANG) $(AS16FLAGS) -o $@ $<

$(BUILD)/trampoline.elf: $(BUILD)/$(KERNEL)/arch/x86_64/ap_trampoline.o linker/trampoline.ld
	$(LLD) -m elf_i386 -T linker/trampoline.ld -o $@ $<

$(BUILD)/trampoline.bin: $(BUILD)/trampoline.elf
	$(OBJCOPY) -O binary $< $@

$(TRAMP_OBJ): $(BUILD)/trampoline.bin
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    --rename-section .data=.trampoline,alloc,load,readonly,data,contents \
	    $< $@

# ============================================================================
# 内核对象编译
# ============================================================================

$(BUILD)/$(KERNEL)/%.o: $(KERNEL)/%.c
	$(CLANG) $(CFLAGS) -o $@ $<

$(BUILD)/$(LIBC)/%.o: $(LIBC)/%.c
	$(CLANG) $(CFLAGS) -o $@ $<

$(BUILD)/$(KERNEL)/%.o: $(KERNEL)/%.S
	$(CLANG) $(AS64FLAGS) -o $@ $<

# ============================================================================
# 内核链接
# ============================================================================

$(BUILD)/kernel.elf: $(KERNEL_S_OBJS) $(KERNEL_C_OBJS) $(TRAMP_OBJ) linker/kernel.ld
	$(LLD) -m elf_x86_64 -T linker/kernel.ld \
	    -o $@ \
	    $(KERNEL_S_OBJS) $(KERNEL_C_OBJS) $(TRAMP_OBJ)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary \
	    -j .text -j .rodata -j .data -j .bss \
	    $< $@

# ============================================================================
# 磁盘镜像构建 (Bootloader + Setup + Kernel)
# 架构师注: 您使用 echo 去除 wc -c 前导空格的 workaround 非常精妙，完美兼容 MSYS2
# ============================================================================

$(BUILD)/os.img: $(BUILD)/kernel.bin boot/setup.ld boot/boot.ld \
                 $(BOOT)/setup.S $(BOOT)/boot.S
	@set -e; \
	CL="$(CLANG)"; LD="$(LLD)"; OC="$(OBJCOPY)"; \
	BD="$(BUILD)"; BT="$(BOOT)"; \
	echo "=== Building setup pass 1 (placeholder) ==="; \
	$$CL $(AS16FLAGS) -DKERNEL_LBA=65 -DKERNEL_SECTORS=256 \
	    -o $$BD/$$BT/setup.o $(BOOT)/setup.S; \
	$$LD -m elf_i386 -T boot/setup.ld \
	    -o $$BD/setup.elf $$BD/$$BT/setup.o; \
	$$OC -O binary -j .text -j .rodata -j .data \
	    $$BD/setup.elf $$BD/setup.bin; \
	echo "=== Computing sector counts ==="; \
	S2SZ=$$(wc -c < $$BD/setup.bin); \
	S2SZ=$$(echo $$S2SZ); \
	S2S=$$(( ($$S2SZ + 511) / 512 )); \
	KLBA=$$(( 1 + $$S2S )); \
	KSIZE=$$(wc -c < $$BD/kernel.bin); \
	KSIZE=$$(echo $$KSIZE); \
	KS=$$(( ($$KSIZE + 511) / 512 )); \
	echo "  setup: $$S2SZ bytes = $$S2S sectors"; \
	echo "  kernel: $$KSIZE bytes = $$KS sectors, LBA=$$KLBA"; \
	echo "=== Building boot ==="; \
	$$CL $(AS16FLAGS) -DSTAGE2_SECTORS=$$S2S \
	    -o $$BD/$$BT/boot.o $(BOOT)/boot.S; \
	$$LD -m elf_i386 -T boot/boot.ld \
	    -o $$BD/boot.elf $$BD/$$BT/boot.o; \
	$$OC -O binary $$BD/boot.elf $$BD/boot.bin; \
	S1SZ=$$(wc -c < $$BD/boot.bin); \
	S1SZ=$$(echo $$S1SZ); \
	if [ "$$S1SZ" -ne 512 ]; then \
	    echo "ERROR: boot.bin = $$S1SZ bytes (must be 512)"; exit 1; \
	fi; \
	echo "  boot: 512 bytes OK"; \
	echo "=== Building setup pass 2 (KERNEL_LBA=$$KLBA KERNEL_SECTORS=$$KS) ==="; \
	$$CL $(AS16FLAGS) -DKERNEL_LBA=$$KLBA -DKERNEL_SECTORS=$$KS \
	    -o $$BD/$$BT/setup.o $(BOOT)/setup.S; \
	$$LD -m elf_i386 -T boot/setup.ld \
	    -o $$BD/setup.elf $$BD/$$BT/setup.o; \
	$$OC -O binary -j .text -j .rodata -j .data \
	    $$BD/setup.elf $$BD/setup.bin; \
	echo "=== Assembling disk image ==="; \
	dd if=/dev/zero of=$@ bs=512 count=8192 status=none; \
	dd if=$$BD/boot.bin of=$@ bs=512 conv=notrunc status=none; \
	dd if=$$BD/setup.bin of=$@ bs=512 seek=1 conv=notrunc status=none; \
	dd if=$$BD/kernel.bin of=$@ bs=512 seek=$$KLBA conv=notrunc status=none; \
	echo "os.img ready: boot@0 setup@1($$S2S s) kernel@$$KLBA($$KS s)"

# ============================================================================
# 运行与调试目标
# ============================================================================

QEMU_ARGS := \
    -drive file=$(BUILD)/os.img,format=raw,index=0,media=disk \
    -m 512M \
    -smp 4 \
    -vga vmware \
    -no-reboot \
	-serial stdio \
    -no-shutdown \
	--display sdl


QEMU_DEBUG_ARGS := \
    -drive file=$(BUILD)/os.img,format=raw,index=0,media=disk \
    -m 512M -smp 4 -vga std -no-reboot -serial stdio -no-shutdown --display sdl \
    -d int,cpu_reset,guest_errors,unimp -D $(BUILD)/qemu.log

run: $(BUILD)/os.img
	$(QEMU) $(QEMU_ARGS)

test: $(BUILD)/os.img
	$(QEMU) $(QEMU_DEBUG_ARGS)

debug: $(BUILD)/os.img
	$(QEMU) $(QEMU_ARGS) -s -S

clean:
	rm -rf $(BUILD)
	rm -f $(KERNEL)/arch/x86_64/irq_vectors.S