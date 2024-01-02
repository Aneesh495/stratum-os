# Stratum OS

Stratum is an original multiprocessor operating system for x86-64 systems featuring an original UEFI loader, a preemptive multiprocessor kernel, isolated native user programs, an original journaled filesystem (StrataFS), and an original network stack.

## Architecture Highlights
- **Platform**: x86-64, UEFI boot, QEMU `q35` machine model with pinned OVMF firmware.
- **Multiprocessing**: Symmetric multiprocessing (SMP) supporting 1, 2, 4, and 8 virtual CPUs via local APIC, I/O APIC, and inter-processor interrupts (IPI).
- **Memory Management**: Four-level canonical paging, physical buddy page allocator, slab object allocator, demand paging, and copy-on-write (COW) address spaces.
- **Drivers**: Modern PCI virtio transport with split virtqueues for block and network devices, 16550A serial UART console, and linear framebuffer text console.
- **Storage**: StrataFS, an original filesystem with block caching, metadata transactions, redo journaling, and atomic durability via virtio storage barriers.
- **Networking**: Guest-native link-to-transport stack implementing Ethernet, ARP, IPv4, ICMP echo, UDP datagrams, and reliable streaming TCP with sliding window flow control.
- **Native User Space**: Ring 3 execution, static ELF64 executable loader, small native C runtime, interactive shell, process supervision (`init`), and a durable transactional ledger service (`ledgerd`).

## Supported Hardware and Virtual Profiles
- Machine: QEMU `q35` with modern PCI.
- CPUs: 1, 2, 4, or 8 virtual CPUs (x86-64 with SSE, NX, APIC).
- Memory Configurations:
  - Standard profile: 256 MiB RAM.
  - Stress profile: 1 GiB RAM.
  - Degraded profile: 64 MiB RAM (reaches working serial shell).
- Firmware: Pinned OVMF EDK2 x86-64 (`edk2-x86_64-code.fd`).

## Repository Organization
```
boot/uefi/          Original UEFI application loader
kernel/arch/x86_64/ Architectural state, traps, APIC, SMP startup
kernel/core/        Initialization, panic, errors, runtime primitives
kernel/mm/          Physical allocator, slab heap, virtual memory, COW
kernel/sched/       Preemptive scheduler, run queues, timers, wait queues
kernel/sync/        Spinlocks, sleeping mutexes, futexes
kernel/proc/        Process lifecycle, ELF loader, descriptors, syscalls
kernel/ipc/         Pipes, local channels, pollable readiness
kernel/drivers/     UART, framebuffer, PCI, virtio block and net
kernel/block/       Block cache, requests, writeback barriers
kernel/fs/          VFS, initramfs, tmpfs, StrataFS journaled filesystem
kernel/net/         Buffers, Ethernet, ARP, IPv4, ICMP, UDP, TCP sockets
kernel/debug/       Structured circular trace buffers, panic symbolizer
include/            Internal kernel headers and user ABI definitions
user/lib/           Native runtime, system call stubs, string/memory library
user/apps/          Init, shell, core utilities, ledger service
tools/              Disk image builder, independent StrataFS checker
tests/host/         Deterministic unit tests and algorithm models
tests/guest/        Real kernel and ring 3 integration tests
tests/faults/       Fault injection, corruption, and crash recovery
bench/              Performance benchmarks and measurement harness
docs/spec/          Frozen interface specifications
docs/adr/           Architectural decision records
scripts/            Build, verification, and acceptance orchestration
```

## Quickstart

### Prerequisites
- Clang and LLD (LLVM 18+ or Homebrew LLVM 20+)
- NASM assembler (2.16+)
- QEMU x86-64 emulator (`qemu-system-x86_64` 8.0+)
- OVMF UEFI firmware
- GNU mtools (`mformat`, `mcopy`)
- Python 3.10+

### Verifying Environment
```bash
make doctor
```

### Building the Operating System
```bash
make build
make image
```

### Running Stratum
```bash
# Interactive boot in QEMU (256 MiB, 4 vCPUs)
make run

# Debug boot with GDB stub attached
make debug
```

### Running Test Suites
```bash
# Run deterministic host tests and models
make test

# Run subsystem test gates
make test-boot
make test-mm
make test-smp
make test-abi
make test-storage
make test-net
make test-faults
```

## Current Limitations
- Dynamic linking (shared libraries) is not supported; all user executables are static ELF64 binaries.
- IPv6 is not implemented; the network stack is strictly IPv4.
- IPv4 packet fragmentation is explicitly rejected and dropped.
- Physical ACPI power management sleep states (S3/S4) are not implemented.
