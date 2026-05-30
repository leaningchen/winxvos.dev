# WinixOS Makefile
# Toolchain: Clang (ELF output) + LLD (ELF linker)
# MSYS2 GNU as/ld only supports PE/COFF; Clang -target generates ELF

CLANG   := /d/CYGWIN/MSYS64/mingw64/bin/clang
LLD     := /d/CYGWIN/MSYS64/mingw64/bin/ld.lld.exe
OBJCOPY := /d/CYGWIN/MSYS64/mingw64/bin/objcopy.exe
QEMU    := qemu-system-x86_64

BUILD   := build
BOOT    := boot
KERNEL  := kernel
LIB     := kernel/lib
USER    := user

# Assembly flags: -target produces ELF, not PE
AS16FLAGS := -target i386-unknown-linux-gnu -ffreestanding -nostdinc -c
AS64FLAGS := -target x86_64-unknown-linux-gnu -ffreestanding -nostdinc -c

CFLAGS := -target x86_64-unknown-linux-gnu \
          -m64 -std=c11 \
          -ffreestanding -fno-stack-protector \
          -fno-pic -mno-red-zone \
          -mcmodel=large \
          -nostdlib -nostdinc \
          -I include -I . -I kernel \
          -O2 -Wall -Wextra -Wno-unused-parameter \
          -c

# 用户程序编译标志（小内存模型，PIC 禁止，用户态库）
UCFLAGS := -target x86_64-unknown-linux-gnu \
           -m64 -std=c11 \
           -ffreestanding -fno-stack-protector \
           -fno-pic -mno-red-zone \
           -mcmodel=small \
           -nostdlib -nostdinc \
           -I user \
           -O2 -Wall -Wextra -Wno-unused-parameter \
           -c

# Kernel C sources
KERNEL_C_SRCS := \
    $(KERNEL)/main.c     \
    $(KERNEL)/video.c    \
    $(KERNEL)/acpi.c     \
    $(KERNEL)/smp.c      \
    $(KERNEL)/font.c     \
    $(KERNEL)/kalloc.c   \
    $(KERNEL)/spinlock.c \
    $(KERNEL)/idt.c      \
    $(KERNEL)/panic.c    \
    $(KERNEL)/cpu.c      \
    $(KERNEL)/pic.c      \
    $(KERNEL)/lapic.c    \
    $(KERNEL)/vm.c       \
    $(KERNEL)/tss.c      \
    $(KERNEL)/proc.c      \
    $(KERNEL)/sleeplock.c \
    $(KERNEL)/syscall.c   \
    $(KERNEL)/sysproc.c   \
    $(KERNEL)/sysfile.c   \
    $(KERNEL)/exec.c      \
    $(KERNEL)/ide.c       \
    $(KERNEL)/bio.c       \
    $(KERNEL)/log.c       \
    $(KERNEL)/fs.c        \
    $(KERNEL)/file.c      \
    $(KERNEL)/pipe.c      \
    $(LIB)/string.c      \
    $(LIB)/ctype.c       \
    $(LIB)/stdio.c       \
    $(LIB)/assert.c

# Kernel assembly sources
KERNEL_S_SRCS := \
    $(KERNEL)/entry64.S        \
    $(KERNEL)/trap_entry.S     \
    $(KERNEL)/irq_vectors.S    \
    $(KERNEL)/swtch.S          \
    $(KERNEL)/syscall_entry.S

KERNEL_C_OBJS  := $(patsubst %.c, $(BUILD)/%.o, $(KERNEL_C_SRCS))
KERNEL_S_OBJS  := $(patsubst %.S, $(BUILD)/%.o, $(KERNEL_S_SRCS))
TRAMP_OBJ      := $(BUILD)/$(KERNEL)/ap_trampoline_raw.o
INITCODE_OBJ   := $(BUILD)/$(KERNEL)/initcode_raw.o

# 用户程序列表（添加新程序只需在此追加）
USER_PROGS := init sh

USER_ELFS  := $(patsubst %, $(BUILD)/$(USER)/%.elf, $(USER_PROGS))
USER_BINS  := $(patsubst %, $(BUILD)/$(USER)/%, $(USER_PROGS))

# 用户公共目标文件（usys.o + ulib.o）
ULIB_OBJS := $(BUILD)/$(USER)/usys.o $(BUILD)/$(USER)/ulib.o

.PHONY: all clean run debug dirs gen-irq

all: dirs gen-irq $(BUILD)/os.img $(BUILD)/fs.img

dirs:
	mkdir -p $(BUILD)/$(BOOT) $(BUILD)/$(KERNEL) $(BUILD)/$(LIB) $(BUILD)/$(USER)

# Generate IRQ handler stubs from script
gen-irq:
	python3 tools/gen_irq_vectors.py $(KERNEL)/irq_vectors.S

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

# initcode: 编译为平坦二进制再嵌入内核 ELF
$(BUILD)/$(KERNEL)/initcode.o: $(KERNEL)/initcode.S
	$(CLANG) $(AS64FLAGS) -o $@ $<

$(BUILD)/initcode.elf: $(BUILD)/$(KERNEL)/initcode.o linker/initcode.ld
	$(LLD) -m elf_x86_64 -T linker/initcode.ld -o $@ $<

$(BUILD)/initcode.bin: $(BUILD)/initcode.elf
	$(OBJCOPY) -O binary $< $@

$(INITCODE_OBJ): $(BUILD)/initcode.bin
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    --rename-section .data=.initcode,alloc,load,readonly,data,contents \
	    --redefine-sym _binary_build_initcode_bin_start=_binary_initcode_start \
	    --redefine-sym _binary_build_initcode_bin_end=_binary_initcode_end \
	    --redefine-sym _binary_build_initcode_bin_size=_binary_initcode_size \
	    $< $@

# Kernel C objects (x86-64 ELF)
$(BUILD)/$(KERNEL)/%.o: $(KERNEL)/%.c
	$(CLANG) $(CFLAGS) -o $@ $<

$(BUILD)/$(LIB)/%.o: $(LIB)/%.c
	$(CLANG) $(CFLAGS) -o $@ $<

# Kernel 64-bit assembly objects
$(BUILD)/$(KERNEL)/%.o: $(KERNEL)/%.S
	$(CLANG) $(AS64FLAGS) -o $@ $<

# Link kernel ELF
$(BUILD)/kernel.elf: $(KERNEL_S_OBJS) $(KERNEL_C_OBJS) $(TRAMP_OBJ) $(INITCODE_OBJ) linker/kernel.ld
	$(LLD) -m elf_x86_64 -T linker/kernel.ld \
	    -o $@ \
	    $(KERNEL_S_OBJS) $(KERNEL_C_OBJS) $(TRAMP_OBJ) $(INITCODE_OBJ)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary \
	    -j .text -j .rodata -j .data -j .bss -j .initcode \
	    $< $@

# ============================================================
# 用户程序构建
# ============================================================

# 用户汇编库（usys.S）
$(BUILD)/$(USER)/usys.o: $(USER)/usys.S
	$(CLANG) $(AS64FLAGS) -o $@ $<

# 用户 C 库（ulib.c）
$(BUILD)/$(USER)/ulib.o: $(USER)/ulib.c
	$(CLANG) $(UCFLAGS) -o $@ $<

# 用户程序 C 对象（init.c, sh.c 等）
$(BUILD)/$(USER)/%.o: $(USER)/%.c
	$(CLANG) $(UCFLAGS) -o $@ $<

# 链接用户 ELF（-e main 作为入口）
$(BUILD)/$(USER)/%.elf: $(BUILD)/$(USER)/%.o $(ULIB_OBJS) linker/user.ld
	$(LLD) -m elf_x86_64 -T linker/user.ld \
	    --entry main \
	    -o $@ $< $(ULIB_OBJS)

# 复制为不带 .elf 后缀的二进制（mkfs 用这些路径）
$(BUILD)/$(USER)/%: $(BUILD)/$(USER)/%.elf
	cp $< $@

# ============================================================
# mkfs: 主机工具（用本机 gcc 编译）
# ============================================================
$(BUILD)/mkfs: tools/mkfs.c
	gcc -O2 -o $@ $<

# ============================================================
# fs.img: 由 mkfs 生成的文件系统镜像
# ============================================================
$(BUILD)/fs.img: $(BUILD)/mkfs $(USER_BINS)
	$(BUILD)/mkfs $@ $(USER_BINS)
	@echo "fs.img ready: $(USER_BINS)"

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

# Run targets (两块硬盘: index=0 内核, index=1 文件系统)
QEMU_ARGS := \
    -drive file=$(BUILD)/os.img,format=raw,index=0,media=disk \
    -drive file=$(BUILD)/fs.img,format=raw,index=1,media=disk \
    -m 256M \
    -smp 4 \
    -vga vmware \
    -no-reboot \
    -no-shutdown

run: all
	$(QEMU) $(QEMU_ARGS)

debug: all
	$(QEMU) $(QEMU_ARGS) -s -S

clean:
	rm -rf $(BUILD)
	rm -f $(KERNEL)/irq_vectors.S