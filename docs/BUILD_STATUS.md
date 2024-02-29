# Stratum OS Build Status

## Overview
- Current Phase: P12 Original Network Stack (TCP/IP)
- Status: in progress
- Repository: Aneesh495/stratum-os
- Target Architecture: x86-64 (UEFI boot, QEMU q35, TCG and acceleration)

## Phase Gates Summary
| Gate | Description | Status |
| --- | --- | --- |
| P01 | Foundation and Toolchain | passed |
| P02 | Original Boot and UEFI Loader | passed |
| P03 | CPU State, Traps, and Diagnostics | passed |
| P04 | Physical and Virtual Memory | passed |
| P05 | SMP Startup and Synchronization | passed |
| P06 | Preemptive SMP Scheduler | passed |
| P07 | User ABI, ELF Loading, Ring 3 | passed |
| P08 | Processes, Threads, and IPC | passed |
| P09 | PCI and Virtio Hardware I/O | passed |
| P10 | VFS and StrataFS Storage | passed |
| P11 | Journal Transactions and Crash Recovery | passed |
| P12 | Original Network Stack (TCP/IP) | in progress |
| P13 | Native User Environment and Ledger Service | unverified |
| P14 | Depth, Concurrency, and Soak Verification | unverified |
| P15 | Performance Benchmarks and Documentation | unverified |
| P16 | Final Acceptance and Reproducibility | unverified |

## Acceptance Workloads (A01 - A12)
| Gate | Workload | Status |
| --- | --- | --- |
| A01 | Boot and image profiles (1, 2, 4, 8 CPUs; 64M, 256M, 1G) | passed |
| A02 | Memory allocation, mapping, COW, and TLB invalidation | passed |
| A03 | Scheduling preemption, SMP live threads (500 threads / 32 procs) | passed |
| A04 | Lifecycle, fork/exec/wait, threads, syscall boundaries | passed |
| A05 | IPC, pipes, message channels, pollable readiness | passed |
| A06 | Virtio block/net drivers, queue index wrapping, error handling | passed |
| A07 | StrataFS operations, journal transactions, crash recovery | passed |
| A08 | Network stack: Ethernet, ARP, IPv4, ICMP, UDP, TCP streams | unverified |
| A09 | Integrated durable ledger service across two guests | unverified |
| A10 | Soak workload (60 min mixed guest on 8 CPUs, 1 GiB) | unverified |
| A11 | Fuzz testing (parsers/decoders) and model exploration | unverified |
| A12 | Reproducibility, two clean builds, verifier negative controls | unverified |

## Latest Command Results
- `scripts/doctor.py`: passed (Clang 23.1.2, LLD 23.1.2, NASM 2.16.03, llvm-objcopy, mtools 4.0.49, QEMU 11.1.1, OVMF hash verified)
- `scripts/scope_check.py`: passed (6530 substantive kernel lines)
- `scripts/test_boot.py`: passed (1, 2, 4, 8 vCPUs, 64M, 256M, 1G profiles, 7/7 malformed loader rejections)
- `scripts/test_cpu_faults.py`: passed (normal boot + deliberate #UD fault decoded with register dump)
- `scripts/test_memory.py`: passed (host MM model 2M ops ASan/UBSan + guest PMM/SLAB/VMM 4-level paging)
- `scripts/test_smp.py`: passed (1, 2, 4, 8 vCPUs with AP trampoline, per-CPU structures via GS, spinlocks/mutexes, TLB shootdown IPI)
- `scripts/test_sched.py`: passed (1, 2, 4, 8 vCPUs preemptive SMP scheduling, O(1) multi-level priority queues, work stealing, sleep/wake, Gate A03 passed)
- `scripts/test_abi.py`: passed (1, 2, 4, 8 vCPUs User ABI, safe usercopy fault recovery, ELF64 loader, Ring 3 entry, syscalls SYS_write/SYS_getpid/SYS_nanosleep/SYS_exit)
- `scripts/test_process_ipc.py`: passed (1, 2, 4, 8 vCPUs process lifecycle, fork, COW memory isolation, IPC pipes, poll readiness, waitpid status propagation, dup2 redirection, Gates A04 and A05 passed)
- `scripts/test_pci_virtio.py`: passed (1, 2, 4, 8 vCPUs PCI bus hierarchy enumeration, BAR probing, capability walking, Virtio modern/legacy transport, Virtio-blk synchronous read/write/flush/cycling, Virtio-net MAC and TX broadcast frame, Gate A06 passed)
- `scripts/test_stratafs.py`: passed (1, 2, 4, 8 vCPUs VFS mount table, StrataFS on-disk layout, WAL journal circular buffer, multi-level directory hierarchy, multi-block contiguous files, indirect block addressing, stat metadata, checkpoint remount persistence, unlink file reclamation, Gate A07 passed)

## Current Architectural Decisions
- ADR-0001: Pinned LLVM 23.1.2 toolchain with Clang and LLD targeting x86_64-unknown-windows for UEFI PE/COFF loader and x86_64-unknown-none-elf for kernel.
- ADR-0002: Modular monolithic kernel structure with freestanding C17 and x86-64 assembly modules.
- ADR-0003: Versioned boot handoff contract between UEFI loader and kernel.
- ADR-0004: Partitioned MBR ESP format for UEFI boot disk with FAT32 partition at 1 MiB offset.
- ADR-0005: 64-bit GDT/TSS with dedicated IST1 for Double Fault and IST2 for NMI handlers.
- ADR-0006: Physical Memory Manager dynamically scopes page structure array to conventional RAM extents, maintaining low memory footprints down to 64 MiB profiles.
- ADR-0007: Virtual Memory Manager establishes 4-level paging with HHDM at 0xFFFF800000000000 and page fault COW resolution.
- ADR-0008: SMP bootstrap utilizes dedicated 16-bit/32-bit/64-bit real-mode trampoline at physical 0x8000, passing kernel PML4 and stack parameters via physical mailbox at 0x8F00.
- ADR-0009: Per-CPU state isolation is established via MSR_GS_BASE, providing each core dedicated TSS, IST stacks, and Local APIC registration.
- ADR-0010: Preemptive SMP scheduling implements O(1) priority queues, work stealing across runqueues, APIC periodic timer preemption (vector 0x20), and assembly context switching with SysV ABI compliance.
- ADR-0011: VFS abstraction decouples file operations from filesystem implementations, supporting hierarchical path resolution, inode lifecycle refcounting, and standard POSIX-like file descriptor operations.
- ADR-0012: StrataFS features 4096-byte blocks, direct and single-indirect block pointers, directory record indexing, and an atomic write-ahead logging (WAL) journal with CRC32 checksums for full crash consistency and redo recovery.

## Unresolved Defects
- None currently recorded.

## Next Concrete Action
- Implement Phase P12: Original Network Stack (TCP/IP) for Gate A08 (Ethernet II frame parsing/framing, ARP cache resolution, IPv4 routing/fragmentation/checksums, ICMP echo responder, UDP sockets, TCP 3-way handshake, sliding window sequence tracking, retransmission, and socket stream API).
