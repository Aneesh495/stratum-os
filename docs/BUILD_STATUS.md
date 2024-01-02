# Stratum OS Build Status

## Overview
- Current Phase: P01 Foundation
- Status: in progress
- Repository: Aneesh495/stratum-os
- Target Architecture: x86-64 (UEFI boot, QEMU q35, TCG and acceleration)

## Phase Gates Summary
| Gate | Description | Status |
| --- | --- | --- |
| P01 | Foundation and Toolchain | in progress |
| P02 | Original Boot and UEFI Loader | unverified |
| P03 | CPU State, Traps, and Diagnostics | unverified |
| P04 | Physical and Virtual Memory | unverified |
| P05 | SMP Startup and Synchronization | unverified |
| P06 | Preemptive SMP Scheduler | unverified |
| P07 | User ABI, ELF Loading, Ring 3 | unverified |
| P08 | Processes, Threads, and IPC | unverified |
| P09 | PCI and Virtio Hardware I/O | unverified |
| P10 | VFS and StrataFS Storage | unverified |
| P11 | Journal Transactions and Crash Recovery | unverified |
| P12 | Original Network Stack (TCP/IP) | unverified |
| P13 | Native User Environment and Ledger Service | unverified |
| P14 | Depth, Concurrency, and Soak Verification | unverified |
| P15 | Performance Benchmarks and Documentation | unverified |
| P16 | Final Acceptance and Reproducibility | unverified |

## Acceptance Workloads (A01 - A12)
| Gate | Workload | Status |
| --- | --- | --- |
| A01 | Boot and image profiles (1, 2, 4, 8 CPUs; 64M, 256M, 1G) | unverified |
| A02 | Memory allocation, mapping, COW, and TLB invalidation | unverified |
| A03 | Scheduling preemption, SMP live threads (500 threads / 32 procs) | unverified |
| A04 | Lifecycle, fork/exec/wait, threads, syscall boundaries | unverified |
| A05 | IPC, pipes, message channels, pollable readiness | unverified |
| A06 | Virtio block/net drivers, queue index wrapping, error handling | unverified |
| A07 | StrataFS operations, journal transactions, crash recovery | unverified |
| A08 | Network stack: Ethernet, ARP, IPv4, ICMP, UDP, TCP streams | unverified |
| A09 | Integrated durable ledger service across two guests | unverified |
| A10 | Soak workload (60 min mixed guest on 8 CPUs, 1 GiB) | unverified |
| A11 | Fuzz testing (parsers/decoders) and model exploration | unverified |
| A12 | Reproducibility, two clean builds, verifier negative controls | unverified |

## Latest Command Results
- `scripts/doctor.py`: passed (Clang 23.1.2, LLD 23.1.2, NASM 2.16.03, llvm-objcopy, mtools 4.0.49, QEMU 11.1.1, OVMF hash verified)
- `scripts/scope_check.py`: passed (baseline ledger initialized in .agent-local/scope_ledger.json)

## Current Architectural Decisions
- ADR-0001: Pinned LLVM 23.1.2 toolchain with Clang and LLD targeting x86_64-unknown-windows for UEFI PE/COFF loader and x86_64-unknown-none-elf for kernel.
- ADR-0002: Modular monolithic kernel structure with freestanding C17 and x86-64 assembly modules.
- ADR-0003: Versioned boot handoff contract between UEFI loader and kernel.

## Unresolved Defects
- None currently recorded.

## Next Concrete Action
- Write initial contract specifications in docs/spec/ and docs/ACCEPTANCE_PLAN.md.
- Construct root Makefile with standard targets.
- Create initial git commit and publish to GitHub repository Aneesh495/stratum-os.
