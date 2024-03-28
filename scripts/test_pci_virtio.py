#!/usr/bin/env python3
"""
scripts/test_pci_virtio.py - Automated tests for Phase P09 PCI and Virtio Hardware I/O (Gate A06).
Verifies:
- PCI bus enumeration (buses, slots, functions, BARs, capabilities)
- Virtio modern/legacy transport and feature negotiation
- Virtio-blk block storage driver: capacity detection, sector read/write, flush, queue index cycling
- Virtio-net network driver: MAC detection, TX packet transmission
- Multi-core SMP concurrency across 1, 2, 4, 8 vCPUs
"""

import os
import subprocess
import sys
import time

QEMU_BIN = "/opt/homebrew/bin/qemu-system-x86_64"
OVMF_BIN = "/opt/homebrew/share/qemu/edk2-x86_64-code.fd"
BOOT_IMG = "build/images/stratum-boot.img"
DATA_IMG = "build/images/stratum-data.img"


def run_pci_virtio_test(cpus):
    print(f"Testing PCI & Virtio Hardware I/O ({cpus} vCPUs)... ", end="", flush=True)
    log_path = f"build/serial_pci_virtio_{cpus}.log"
    if os.path.exists(log_path):
        os.remove(log_path)

    cmd = [
        QEMU_BIN,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64,+nx,+apic",
        "-smp", str(cpus),
        "-m", "256M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_BIN}",
        "-drive", f"file={BOOT_IMG},if=none,id=bootdisk,format=raw",
        "-device", "ide-hd,drive=bootdisk,bootindex=1",
        "-drive", f"file={DATA_IMG},if=none,id=datadisk,format=raw",
        "-device", "virtio-blk-pci,drive=datadisk",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-display", "none",
        "-serial", "stdio"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    start = time.time()

    full_output = []
    while time.time() - start < 30:
        line = proc.stdout.readline()
        if not line and proc.poll() is not None:
            break
        if line:
            full_output.append(line)
            if "Gate A06 passed" in line:
                break

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    content = "".join(full_output)
    with open(log_path, "w") as f:
        f.write(content)

    pci_enum_ok = "Scanning PCI bus hierarchy" in content and "Enumeration complete" in content
    blk_init_ok = "Block device initialized: capacity=" in content
    blk_io_ok = "Virtio-blk synchronous read, write, flush, and queue index cycling verified" in content
    net_init_ok = "Network device initialized: MAC=" in content
    net_tx_ok = "Virtio-net MAC identification and TX frame transmission verified" in content
    p09_pass_ok = "Gate A06 passed" in content

    all_passed = (pci_enum_ok and blk_init_ok and blk_io_ok and
                  net_init_ok and net_tx_ok and p09_pass_ok)

    if all_passed:
        print("PASSED")
        return True
    else:
        print("FAILED")
        print(f"  pci_enum_ok={pci_enum_ok}, blk_init_ok={blk_init_ok}, blk_io_ok={blk_io_ok}")
        print(f"  net_init_ok={net_init_ok}, net_tx_ok={net_tx_ok}, p09_pass_ok={p09_pass_ok}")
        if os.path.exists(log_path):
            print("--- Serial Log Tail ---")
            with open(log_path, "r", errors="ignore") as f:
                lines = f.readlines()
                print("".join(lines[-35:]))
        return False


def main():
    print("=== Stratum Phase P09 Verification Test (Gate A06) ===")
    cpu_configs = [1, 2, 4, 8]
    for cpus in cpu_configs:
        if not run_pci_virtio_test(cpus):
            sys.exit(1)
    print("\n[ALL PASSED] Phase P09 Gate A06 verified across 1, 2, 4, 8 vCPUs.")
    sys.exit(0)


if __name__ == "__main__":
    main()
