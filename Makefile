# WinixOS Bootloader Makefile
# Toolchain: Clang (ELF output) + LLD (ELF linker)
# MSYS2 GNU as/ld only supports PE/COFF; Clang -target generates ELF

CLANG   := /d/CYGWIN/MSYS64/mingw64/bin/clang
LLD     := /d/CYGWIN/MSYS64/mingw64/bin/ld.lld.exe
OBJCOPY := /d/CYGWIN/MSYS64/mingw64/bin/objcopy.exe
QEMU    := qemu-system-x86_64

BUILD   := build
BOOT    := boot
KERNEL  := kernel

# Assembly flags: -target produces ELF, not PE
AS16FLAGS := -target i386-unknown-linux-gnu -ffreestanding -nostdinc -c
AS64FLAGS := -target x86_64-unknown-linux-gnu -ffreestanding -nostdinc -c

CFLAGS := -target x86_64-unknown-linux-gnu \
          -m64 -std=c11 \
          -ffreestanding -fno-stack-protector \
          -fno-pic -mno-red-zone \
          -nostdlib -nostdinc \
          -I include -I . \
          -O2 -Wall -Wextra -Wno-unused-parameter \
          -c

KERNEL_C_SRCS := \
    $(KERNEL)/main.c  \
    $(KERNEL)/video.c \
    $(KERNEL)/util.c  \
    $(KERNEL)/acpi.c  \
    $(KERNEL)/smp.c   \
    $(KERNEL)/font.c

KERNEL_C_OBJS := $(patsubst %.c, $(BUILD)/%.o, $(KERNEL_C_SRCS))
KERNEL_S_OBJS := $(BUILD)/$(KERNEL)/entry64.o
TRAMP_OBJ     := $(BUILD)/$(KERNEL)/ap_trampoline_raw.o

.PHONY: all clean run debug dirs

all: dirs $(BUILD)/os.img

dirs:
	mkdir -p $(BUILD)/$(BOOT) $(BUILD)/$(KERNEL)

# AP trampoline (i386 ELF)
$(BUILD)/$(KERNEL)/ap_trampoline.o: $(KERNEL)/ap_trampoline.S
	$(CLANG) $(AS16FLAGS) -o $@ $<

$(BUILD)/trampoline.elf: $(BUILD)/$(KERNEL)/ap_trampoline.o linker/trampoline.ld
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

# Kernel 64-bit entry
$(BUILD)/$(KERNEL)/entry64.o: $(KERNEL)/entry64.S
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
$(BUILD)/os.img: $(BUILD)/kernel.bin linker/stage2.ld linker/stage1.ld \
                 $(BOOT)/stage2.S $(BOOT)/stage1.S
	@set -e; \
	CL="$(CLANG)"; LD="$(LLD)"; OC="$(OBJCOPY)"; \
	BD="$(BUILD)"; BT="$(BOOT)"; \
	echo "=== Building stage2 pass 1 (placeholder) ==="; \
	$$CL $(AS16FLAGS) -DKERNEL_LBA=65 -DKERNEL_SECTORS=256 \
	    -o $$BD/$$BT/stage2.o $(BOOT)/stage2.S; \
	$$LD -m elf_i386 -T linker/stage2.ld \
	    -o $$BD/stage2.elf $$BD/$$BT/stage2.o; \
	$$OC -O binary -j .text -j .rodata -j .data \
	    $$BD/stage2.elf $$BD/stage2.bin; \
	echo "=== Computing sector counts ==="; \
	S2SZ=$$(wc -c < $$BD/stage2.bin); \
	S2SZ=$$(echo $$S2SZ); \
	S2S=$$(( ($$S2SZ + 511) / 512 )); \
	KLBA=$$(( 1 + $$S2S )); \
	KSIZE=$$(wc -c < $$BD/kernel.bin); \
	KSIZE=$$(echo $$KSIZE); \
	KS=$$(( ($$KSIZE + 511) / 512 )); \
	echo "  stage2: $$S2SZ bytes = $$S2S sectors"; \
	echo "  kernel: $$KSIZE bytes = $$KS sectors, LBA=$$KLBA"; \
	echo "=== Building stage1 ==="; \
	$$CL $(AS16FLAGS) -DSTAGE2_SECTORS=$$S2S \
	    -o $$BD/$$BT/stage1.o $(BOOT)/stage1.S; \
	$$LD -m elf_i386 -T linker/stage1.ld \
	    -o $$BD/stage1.elf $$BD/$$BT/stage1.o; \
	$$OC -O binary $$BD/stage1.elf $$BD/stage1.bin; \
	S1SZ=$$(wc -c < $$BD/stage1.bin); \
	S1SZ=$$(echo $$S1SZ); \
	if [ "$$S1SZ" -ne 512 ]; then \
	    echo "ERROR: stage1.bin = $$S1SZ bytes (must be 512)"; exit 1; \
	fi; \
	echo "  stage1: 512 bytes OK"; \
	echo "=== Building stage2 pass 2 (KERNEL_LBA=$$KLBA KERNEL_SECTORS=$$KS) ==="; \
	$$CL $(AS16FLAGS) -DKERNEL_LBA=$$KLBA -DKERNEL_SECTORS=$$KS \
	    -o $$BD/$$BT/stage2.o $(BOOT)/stage2.S; \
	$$LD -m elf_i386 -T linker/stage2.ld \
	    -o $$BD/stage2.elf $$BD/$$BT/stage2.o; \
	$$OC -O binary -j .text -j .rodata -j .data \
	    $$BD/stage2.elf $$BD/stage2.bin; \
	echo "=== Assembling disk image ==="; \
	dd if=/dev/zero of=$@ bs=512 count=8192 status=none; \
	dd if=$$BD/stage1.bin of=$@ bs=512 conv=notrunc status=none; \
	dd if=$$BD/stage2.bin of=$@ bs=512 seek=1 conv=notrunc status=none; \
	dd if=$$BD/kernel.bin of=$@ bs=512 seek=$$KLBA conv=notrunc status=none; \
	echo "os.img ready: stage1@0 stage2@1($$S2S s) kernel@$$KLBA($$KS s)"

# Run targets
QEMU_ARGS := \
    -drive file=$(BUILD)/os.img,format=raw,index=0,media=disk \
    -m 256M \
    -smp 4 \
    -vga vmware \
    -no-reboot \
    -no-shutdown

run: $(BUILD)/os.img
	$(QEMU) $(QEMU_ARGS)

debug: $(BUILD)/os.img
	$(QEMU) $(QEMU_ARGS) -s -S

clean:
	rm -rf $(BUILD)
