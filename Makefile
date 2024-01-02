# Stratum Operating System Root Makefile
# Strict standalone cross-compilation on macOS / Linux

SHELL := /bin/bash

# Pinned Toolchain paths (with environment overrides)
LLVM_PREFIX ?= /opt/homebrew/opt/llvm
CLANG       ?= $(if $(wildcard $(LLVM_PREFIX)/bin/clang),$(LLVM_PREFIX)/bin/clang,clang)
LD_LLD      ?= $(if $(wildcard /opt/homebrew/bin/ld.lld),/opt/homebrew/bin/ld.lld,ld.lld)
LLD         ?= $(if $(wildcard /opt/homebrew/bin/lld),/opt/homebrew/bin/lld,lld)
NASM        ?= $(if $(wildcard /opt/homebrew/bin/nasm),/opt/homebrew/bin/nasm,nasm)
LLVM_OBJCOPY ?= $(if $(wildcard $(LLVM_PREFIX)/bin/llvm-objcopy),$(LLVM_PREFIX)/bin/llvm-objcopy,llvm-objcopy)
MFORMAT     ?= $(if $(wildcard /opt/homebrew/bin/mformat),/opt/homebrew/bin/mformat,mformat)
MCOPY       ?= $(if $(wildcard /opt/homebrew/bin/mcopy),/opt/homebrew/bin/mcopy,mcopy)
MMD         ?= $(if $(wildcard /opt/homebrew/bin/mmd),/opt/homebrew/bin/mmd,mmd)
QEMU        ?= $(if $(wildcard /opt/homebrew/bin/qemu-system-x86_64),/opt/homebrew/bin/qemu-system-x86_64,qemu-system-x86_64)
OVMF        ?= /opt/homebrew/Cellar/qemu/11.1.1/share/qemu/edk2-x86_64-code.fd
PYTHON      ?= python3

BUILD_DIR   := build
BIN_DIR     := bin
IMAGE_DIR   := $(BUILD_DIR)/images

BOOT_LOADER := $(BUILD_DIR)/BOOTX64.EFI
KERNEL_ELF  := $(BUILD_DIR)/stratum.elf
INITRAMFS   := $(BUILD_DIR)/initramfs.cpio
BOOT_DISK   := $(IMAGE_DIR)/stratum-boot.img
DATA_DISK   := $(IMAGE_DIR)/stratum-data.img

# Compiler and Linker Flags
UEFI_CFLAGS := -target x86_64-unknown-windows -ffreestanding -fno-stack-protector \
               -fshort-wchar -mno-red-zone -Wall -Wextra -std=c17 -O2 \
               -Iinclude/boot -Iinclude/shared

KERNEL_CFLAGS := -target x86_64-unknown-none-elf -ffreestanding -mno-red-zone \
                 -mcmodel=kernel -mno-mmx -mno-sse -fno-stack-protector \
                 -fno-omit-frame-pointer -Wall -Wextra -std=c17 -O2 \
                 -Iinclude/kernel -Iinclude/shared

KERNEL_LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T kernel/arch/x86_64/linker.ld

USER_CFLAGS := -target x86_64-unknown-none-elf -ffreestanding -fno-stack-protector \
               -fno-omit-frame-pointer -Wall -Wextra -std=c17 -O2 \
               -Iinclude/user -Iinclude/shared

USER_LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T user/lib/user.ld

.PHONY: all bootstrap doctor build image run debug test test-boot test-mm test-smp \
        test-abi test-storage test-net test-faults fuzz soak bench docs scope-check \
        demo acceptance verify release clean

all: build image

bootstrap:
	@echo "Checking and locating pinned dependencies..."
	@$(PYTHON) scripts/doctor.py

doctor:
	@$(PYTHON) scripts/doctor.py

build:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR) $(IMAGE_DIR)
	@$(MAKE) -f scripts/build_subsystems.mk build-all

image: build
	@mkdir -p $(IMAGE_DIR)
	@$(PYTHON) scripts/build_image.py --boot-img $(BOOT_DISK) --data-img $(DATA_DISK) \
		--loader $(BOOT_LOADER) --kernel $(KERNEL_ELF) --initramfs $(INITRAMFS)

run: image
	@$(PYTHON) scripts/run_vm.py --boot-img $(BOOT_DISK) --data-img $(DATA_DISK) \
		--ovmf $(OVMF) --cpus 4 --mem 256M --serial stdio

debug: image
	@$(PYTHON) scripts/run_vm.py --boot-img $(BOOT_DISK) --data-img $(DATA_DISK) \
		--ovmf $(OVMF) --cpus 4 --mem 256M --serial stdio --gdb 1234

test:
	@$(PYTHON) scripts/run_tests.py --suite all_host

test-boot:
	@$(PYTHON) scripts/run_tests.py --suite boot

test-mm:
	@$(PYTHON) scripts/run_tests.py --suite mm

test-smp:
	@$(PYTHON) scripts/run_tests.py --suite smp

test-abi:
	@$(PYTHON) scripts/run_tests.py --suite abi

test-storage:
	@$(PYTHON) scripts/run_tests.py --suite storage

test-net:
	@$(PYTHON) scripts/run_tests.py --suite net

test-faults:
	@$(PYTHON) scripts/run_tests.py --suite faults

fuzz:
	@$(PYTHON) scripts/run_fuzz.py

soak:
	@$(PYTHON) scripts/run_soak.py

bench:
	@$(PYTHON) scripts/run_bench.py

docs:
	@$(PYTHON) scripts/check_docs.py

scope-check:
	@$(PYTHON) scripts/scope_check.py

demo: image
	@$(PYTHON) scripts/run_demo.py

acceptance:
	@$(PYTHON) scripts/run_acceptance.py

verify:
	@$(PYTHON) scripts/verify_evidence.py

release: verify
	@$(PYTHON) scripts/build_release.py

clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Clean completed."
