# Subsystems build rules for Stratum OS

LLVM_PREFIX ?= /opt/homebrew/opt/llvm
CLANG       ?= $(if $(wildcard $(LLVM_PREFIX)/bin/clang),$(LLVM_PREFIX)/bin/clang,clang)
LD_LLD      ?= $(if $(wildcard /opt/homebrew/bin/ld.lld),/opt/homebrew/bin/ld.lld,ld.lld)
LLD         ?= $(if $(wildcard /opt/homebrew/bin/lld),/opt/homebrew/bin/lld,lld)

BUILD_DIR ?= build

UEFI_SRCS := boot/uefi/main.c boot/uefi/elf.c
UEFI_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(UEFI_SRCS))

KERNEL_CSRCS := kernel/core/string.c \
                kernel/core/kprintf.c \
                kernel/core/boot.c \
                kernel/core/main.c \
                kernel/arch/x86_64/gdt.c \
                kernel/arch/x86_64/idt.c \
                kernel/mm/pmm.c \
                kernel/mm/slab.c \
                kernel/mm/vmm.c \
                kernel/drivers/uart.c \
                kernel/drivers/fb.c

KERNEL_ASMSRCS := kernel/arch/x86_64/entry.S \
                  kernel/arch/x86_64/interrupts.S

KERNEL_COBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(KERNEL_CSRCS))
KERNEL_ASMOBJS := $(patsubst %.S,$(BUILD_DIR)/%.o,$(KERNEL_ASMSRCS))
KERNEL_OBJS := $(KERNEL_COBJS) $(KERNEL_ASMOBJS)

UEFI_CFLAGS := -target x86_64-unknown-windows -ffreestanding -fno-stack-protector \
               -fshort-wchar -mno-red-zone -Wall -Wextra -std=c17 -O2 \
               -Iinclude -Iboot/uefi -Iinclude/boot -Iinclude/shared

KERNEL_CFLAGS := -target x86_64-unknown-none-elf -ffreestanding -mno-red-zone \
                 -mcmodel=kernel -mno-mmx -mno-sse -fno-stack-protector \
                 -fno-omit-frame-pointer -Wall -Wextra -std=c17 -O2 \
                 -Iinclude -Iinclude/kernel -Iinclude/shared

KERNEL_LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T kernel/arch/x86_64/linker.ld

.PHONY: build-all build-loader build-kernel build-initramfs

build-all: build-loader build-kernel build-initramfs

# Build UEFI Loader
build-loader: $(BUILD_DIR)/BOOTX64.EFI

$(BUILD_DIR)/boot/uefi/%.o: boot/uefi/%.c
	@mkdir -p $(dir $@)
	$(CLANG) $(UEFI_CFLAGS) -c $< -o $@

$(BUILD_DIR)/BOOTX64.EFI: $(UEFI_OBJS)
	@mkdir -p $(dir $@)
	$(LLD) -flavor link /subsystem:efi_application /entry:EfiMain /dynamicbase:no /nodefaultlib /out:$@ $^

# Build Kernel ELF
build-kernel: $(BUILD_DIR)/stratum.elf

$(BUILD_DIR)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CLANG) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: kernel/%.S
	@mkdir -p $(dir $@)
	$(CLANG) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/stratum.elf: $(KERNEL_OBJS) kernel/arch/x86_64/linker.ld
	@mkdir -p $(dir $@)
	$(LD_LLD) $(KERNEL_LDFLAGS) -o $@ $(KERNEL_OBJS)

# Build Minimal Initramfs
build-initramfs: $(BUILD_DIR)/initramfs.cpio

$(BUILD_DIR)/initramfs.cpio:
	@mkdir -p $(BUILD_DIR)/initramfs_root
	@echo "Stratum OS Initramfs" > $(BUILD_DIR)/initramfs_root/hello.txt
	@cd $(BUILD_DIR)/initramfs_root && find . | cpio -o -H newc > ../initramfs.cpio 2>/dev/null || touch $@
