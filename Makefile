# WinixOS Makefile
# Toolchain: Clang (ELF output) + LLD (ELF linker)
# MSYS2 GNU as/ld only supports PE/COFF; Clang -target generates ELF

CLANG   := /usr/bin/clang
LLD     := /usr/bin/ld.lld
OBJCOPY := /usr/bin/llvm-objcopy
QEMU    := /mingw64/bin/qemu-system-x86_64

BUILD   := build
BOOT    := boot
KERNEL  := kernel
LIBC    := libc

# Assembly flags: -target produces ELF, not PE
AS16FLAGS := -target i386-unknown-linux-gnu -ffreestanding -nostdinc -c
AS64FLAGS := -target x86_64-unknown-linux-gnu -ffreestanding -nostdinc -c

CFLAGS := -target x86_64-unknown-linux-gnu \
          -m64 -std=c11 \
          -ffreestanding -fno-stack-protector \
          -fno-pic -mno-red-zone \
          -nostdlib -nostdinc \
          -I libc -I . -I kernel \
          -I kernel/arch/x86_64 \
          -I kernel/drivers/video \
          -I kernel/drivers/fs \
          -O2 -Wall -Wextra -Wno-unused-parameter \
          -c

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
	python3 tools/gen_irq_vectors.py $(KERNEL)/arch/x86_64/irq_vectors.S

# AP trampoline (i386 ELF)
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

# Kernel C objects (x86-64 ELF)
$(BUILD)/$(KERNEL)/%.o: $(KERNEL)/%.c
	$(CLANG) $(CFLAGS) -o $@ $<

$(BUILD)/$(LIBC)/%.o: $(LIBC)/%.c
	$(CLANG) $(CFLAGS) -o $@ $<

# Kernel 64-bit assembly objects
$(BUILD)/$(KERNEL)/%.o: $(KERNEL)/%.S
	$(CLANG) $(AS64FLAGS) -o $@ $<

# Link kernel ELF
$(BUILD)/kernel.elf: $(KERNEL_S_OBJS) $(KERNEL_C_OBJS) $(TRAMP_OBJ) linker/kernel.ld
	$(LLD) -m elf_x86_64 -T linker/kernel.ld \
	    -o $@ \
	    $(KERNEL_S_OBJS) $(KERNEL_C_OBJS) $(TRAMP_OBJ)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary \
	    -j .text -j .rodata -j .data -j .bss \
	    $< $@

# ============================================================
# Disk image: single shell script to resolve circular dependency
# ============================================================
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

# Run targets
QEMU_ARGS := \
    -drive file=$(BUILD)/os.img,format=raw,index=0,media=disk \
    -m 512M \
    -smp 4 \
    -vga vmware \
    -no-reboot \
	-serial stdio \
    -no-shutdown \
	--display sdl

run: $(BUILD)/os.img
	$(QEMU) $(QEMU_ARGS)

debug: $(BUILD)/os.img
	$(QEMU) $(QEMU_ARGS) -s -S

clean:
	rm -rf $(BUILD)
	rm -f $(KERNEL)/arch/x86_64/irq_vectors.S