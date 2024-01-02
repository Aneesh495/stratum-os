# Stratum Virtual Memory Layout Specification

Version: 1.0.0
Status: Frozen

## 1. Canonical Address Space (x86-64 4-Level Paging)
Stratum utilizes standard 4-level 48-bit canonical paging (PML4, PDPT, PD, PT) with 4 KiB base pages and optional 2 MiB large pages for direct physical map efficiency.

```
+-----------------------------------+ 0xFFFFFFFFFFFFFFFF
| High Kernel Core and Drivers      | (2 GiB text, data, bss)
| Base: 0xFFFFFFFF80000000          |
+-----------------------------------+ 0xFFFFFFFF7FFFFFFF
| Dynamic Kernel Heap & VM Objects  | (128 GiB)
| Base: 0xFFFFC00000000000          |
+-----------------------------------+ 0xFFFFBFFFFFFFFFFF
| Device MMIO & ACPI Tables         | (64 GiB, NX + Cache-Disable)
| Base: 0xFFFFA00000000000          |
+-----------------------------------+ 0xFFFF9FFFFFFFFFFF
| Direct Physical Map (HHDM)        | (1 TiB direct identity map)
| Base: 0xFFFF800000000000          |
+-----------------------------------+ 0xFFFF800000000000
| Non-Canonical Hole                |
| (Reserved by x86-64 architecture) |
+-----------------------------------+ 0x00007FFFFFFFFFFF
| User Space Upper Limit            |
| - User Stacks (with guard pages)  |
| - Memory mapped files and IPC     |
| - Dynamic Heap (brk / mmap)       |
| - User Executable Text and Data   |
| Base: 0x0000000000400000          |
+-----------------------------------+ 0x0000000000010000
| Zero-Page Guard (Null Trap)       | (64 KiB unmapped)
+-----------------------------------+ 0x0000000000000000
```

## 2. Key Regions and Base Offsets
- High Kernel Base: `0xFFFFFFFF80000000` (Kernel code, rodata, and static data linked at -2 GiB).
- Kernel Heap Base: `0xFFFFC00000000000` (Dynamic slab allocator and vmalloc ranges).
- Device MMIO Base: `0xFFFFA00000000000` (APIC, PCI ECAM, Virtio MMIO mapped with PCD/PWT, NX).
- Direct Physical Map: `0xFFFF800000000000` (Allows kernel to access physical address `P` at `0xFFFF800000000000 + P`).
- User Space Base: `0x0000000000400000` (User executables start at 4 MiB).
- User Space Ceiling: `0x00007FFFFFFFFFFF` (Top of 47-bit canonical user space).

## 3. Protection and Permission Policies
1. Kernel Code: Mapped Present (P=1), Read-Only (R/W=0), Supervisor (U/S=0), Executable (NX=0).
2. Kernel Rodata: Mapped Present (P=1), Read-Only (R/W=0), Supervisor (U/S=0), Non-Executable (NX=1).
3. Kernel Data/BSS: Mapped Present (P=1), Read/Write (R/W=1), Supervisor (U/S=0), Non-Executable (NX=1).
4. Direct Physical Map: Mapped Present, Read/Write, Supervisor, Non-Executable (NX=1). Any alias modification requires TLB flush.
5. User Memory: Supervisor bit cleared (U/S=1). Kernel space is never accessible from ring 3.
6. Guard Pages: Every kernel thread stack and user stack possesses at least one unmapped 4 KiB guard page at its bottom. Stack overflow generates a demand fault or Page Fault exception without corrupting adjacent memory.
7. W^X Policy: No memory region in user space or kernel space is simultaneously writable and executable.
