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
OVMF_BIN = "/opt/homebrew/Cellar/qemu/11.1.1/share/qemu/edk2-x86_64-code.fd"
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
        "-serial", f"file:{log_path}"
    ]

    proc = subprocess.Popen(cmd)
    start = time.time()

    pci_enum_ok = False
    blk_init_ok = False
    blk_io_ok = False
    net_init_ok = False
    net_tx_ok = False
    p09_pass_ok = False

    while time.time() - start < 45:
        if os.path.exists(log_path):
            with open(log_path, "r", errors="ignore") as f:
                content = f.read()
                if "Scanning PCI bus hierarchy" in content and "Enumeration complete" in content:
                    pci_enum_ok = True
                if "Block device initialized: capacity=" in content:
                    blk_init_ok = True
                if "Virtio-blk synchronous read, write, flush, and queue index cycling verified" in content:
                    blk_io_ok = True
                if "Network device initialized: MAC=" in content:
                    net_init_ok = True
                if "Virtio-net MAC identification and TX frame transmission verified" in content:
                    net_tx_ok = True
                if "P09 Virtio block and network hardware I/O verified successfully (Gate A06 passed)" in content:
                    p09_pass_ok = True

                if (pci_enum_ok and blk_init_ok and blk_io_ok and
                    net_init_ok and net_tx_ok and p09_pass_ok):
                    break
        time.sleep(0.2)

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

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
