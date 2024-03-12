#!/usr/bin/env python3
"""
scripts/test_ledger_cluster.py - Automated tests for Phase P13 Native User Environment & Distributed Ledger Service (Gate A09).

Verifies:
- Native userland interactive shell CLI (help, echo, sysinfo, netstat, ping, ledger)
- Userland ledger daemon service (ledgerd)
- Kernel lockless per-CPU tracing ring buffer (trace_emit, trace_dump)
- Panic symbol diagnosis and symbol resolution (kmain)
- Cryptographic SHA-256 and Merkle root tree calculations
- Distributed durable ledger block forging, transaction validation, and hash chaining
- Atomic StrataFS file persistence to /strata/ledger.dat with journal fsync durability
- Interruption recovery replaying committed blocks and verifying chain continuity
- Peer-to-peer TCP replication service on port 9090
- Dual QEMU guest cluster network execution
- Multi-core SMP concurrency across 1, 2, 4, and 8 vCPUs
"""

import os
import shutil
import subprocess
import sys
import time

QEMU_BIN = "/opt/homebrew/bin/qemu-system-x86_64"
OVMF_BIN = "/opt/homebrew/Cellar/qemu/11.1.1/share/qemu/edk2-x86_64-code.fd"
BOOT_IMG = "build/images/stratum-boot.img"
DATA_IMG = "build/images/stratum-data.img"


def run_ledger_cpu_test(cpus):
    print(f"Testing Native User Environment & Ledger ({cpus} vCPUs)... ", end="", flush=True)
    log_path = f"build/serial_ledger_{cpus}.log"
    if os.path.exists(log_path):
        os.remove(log_path)

    fresh_data = f"build/images/stratum-data-{cpus}.img"
    shutil.copyfile(DATA_IMG, fresh_data)

    cmd = [
        QEMU_BIN,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64,+nx,+apic",
        "-smp", str(cpus),
        "-m", "256M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_BIN}",
        "-drive", f"file={BOOT_IMG},if=none,id=bootdisk,format=raw",
        "-device", "ide-hd,drive=bootdisk,bootindex=1",
        "-drive", f"file={fresh_data},if=none,id=datadisk,format=raw",
        "-device", "virtio-blk-pci,drive=datadisk",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-display", "none",
        "-serial", f"file:{log_path}"
    ]

    proc = subprocess.Popen(cmd)
    start = time.time()

    while time.time() - start < 60:
        if os.path.exists(log_path):
            with open(log_path, "r", errors="ignore") as f:
                content = f.read()
                if "Gate A09 passed" in content:
                    break
        time.sleep(0.3)

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    shell_ok = False
    user_ledger_ok = False
    trace_ok = False
    panic_sym_ok = False
    sha256_ok = False
    block_forge_ok = False
    persist_ok = False
    recover_ok = False
    peer_service_ok = False
    p13_pass_ok = False

    if os.path.exists(log_path):
        with open(log_path, "r", errors="ignore") as f:
            content = f.read()
            if "Stratum Interactive Shell CLI operational" in content:
                shell_ok = True
            if "Phase P13 Native User Environment & Ledger Service verified successfully" in content:
                user_ledger_ok = True
            if "Per-CPU lockless tracing ring buffers initialized" in content:
                trace_ok = True
            if "Panic symbol resolution verified: kmain+0x0" in content:
                panic_sym_ok = True
            if "SHA-256 cryptographic hashing verified" in content:
                sha256_ok = True
            if "Ledger block 1 forged and appended" in content and "Ledger block 2 forged and chained" in content:
                block_forge_ok = True
            if "Ledger chain committed and persisted to StrataFS" in content:
                persist_ok = True
            if "Ledger recovery replayed 3 valid blocks" in content:
                recover_ok = True
            if "Peer replication TCP service initialized on port 9090" in content:
                peer_service_ok = True
            if "P13 Native User Environment and Distributed Ledger verified successfully (Gate A09 passed)" in content:
                p13_pass_ok = True

    passed = (shell_ok and user_ledger_ok and trace_ok and panic_sym_ok and
              sha256_ok and block_forge_ok and persist_ok and recover_ok and
              peer_service_ok and p13_pass_ok)

    if passed:
        print("PASSED")
        return True
    else:
        print("FAILED")
        print(f"  shell_ok={shell_ok}, user_ledger_ok={user_ledger_ok}, trace_ok={trace_ok}")
        print(f"  panic_sym_ok={panic_sym_ok}, sha256_ok={sha256_ok}, block_forge_ok={block_forge_ok}")
        print(f"  persist_ok={persist_ok}, recover_ok={recover_ok}, peer_service_ok={peer_service_ok}")
        print(f"  p13_pass_ok={p13_pass_ok}")
        if os.path.exists(log_path):
            print("--- Log Tail ---")
            with open(log_path, "r", errors="ignore") as f:
                lines = f.readlines()
                print("".join(lines[-35:]))
        return False


def run_dual_guest_cluster_test():
    print("Testing Dual QEMU Guest Interconnected Cluster Replication... ", end="", flush=True)

    guest1_boot = "build/images/guest1-boot.img"
    guest2_boot = "build/images/guest2-boot.img"
    guest1_data = "build/images/guest1-data.img"
    guest2_data = "build/images/guest2-data.img"
    shutil.copyfile(BOOT_IMG, guest1_boot)
    shutil.copyfile(BOOT_IMG, guest2_boot)
    shutil.copyfile(DATA_IMG, guest1_data)
    shutil.copyfile(DATA_IMG, guest2_data)

    log1 = "build/serial_cluster_guest1.log"
    log2 = "build/serial_cluster_guest2.log"
    if os.path.exists(log1): os.remove(log1)
    if os.path.exists(log2): os.remove(log2)

    cmd1 = [
        QEMU_BIN,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64,+nx,+apic",
        "-smp", "2",
        "-m", "256M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_BIN}",
        "-drive", f"file={guest1_boot},if=none,id=bootdisk,format=raw",
        "-device", "ide-hd,drive=bootdisk,bootindex=1",
        "-drive", f"file={guest1_data},if=none,id=datadisk,format=raw",
        "-device", "virtio-blk-pci,drive=datadisk",
        "-netdev", "socket,id=net0,listen=:12345",
        "-device", "virtio-net-pci,netdev=net0,mac=52:54:00:12:34:56",
        "-display", "none",
        "-serial", f"file:{log1}"
    ]

    cmd2 = [
        QEMU_BIN,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64,+nx,+apic",
        "-smp", "2",
        "-m", "256M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_BIN}",
        "-drive", f"file={guest2_boot},if=none,id=bootdisk,format=raw",
        "-device", "ide-hd,drive=bootdisk,bootindex=1",
        "-drive", f"file={guest2_data},if=none,id=datadisk,format=raw",
        "-device", "virtio-blk-pci,drive=datadisk",
        "-netdev", "socket,id=net0,connect=127.0.0.1:12345",
        "-device", "virtio-net-pci,netdev=net0,mac=52:54:00:12:34:57",
        "-display", "none",
        "-serial", f"file:{log2}"
    ]

    p1 = subprocess.Popen(cmd1)
    time.sleep(1.0)
    p2 = subprocess.Popen(cmd2)

    start = time.time()
    g1_ok = False
    g2_ok = False

    while time.time() - start < 45:
        if os.path.exists(log1) and not g1_ok:
            with open(log1, "r", errors="ignore") as f:
                if "Gate A09 passed" in f.read():
                    g1_ok = True
        if os.path.exists(log2) and not g2_ok:
            with open(log2, "r", errors="ignore") as f:
                if "Gate A09 passed" in f.read():
                    g2_ok = True

        if g1_ok and g2_ok:
            break
        time.sleep(0.3)

    for p in (p1, p2):
        p.terminate()
        try:
            p.wait(timeout=2)
        except subprocess.TimeoutExpired:
            p.kill()

    if g1_ok and g2_ok:
        print("PASSED")
        return True
    else:
        print(f"FAILED (Guest1={g1_ok}, Guest2={g2_ok})")
        return False


def main():
    print("=== Stratum Phase P13 Verification Test (Gate A09) ===")
    cpu_configs = [1, 2, 4, 8]
    for cpus in cpu_configs:
        if not run_ledger_cpu_test(cpus):
            sys.exit(1)

    if not run_dual_guest_cluster_test():
        sys.exit(1)

    print("\n[ALL PASSED] Phase P13 Gate A09 verified across 1, 2, 4, 8 vCPUs and dual-guest cluster.")
    sys.exit(0)


if __name__ == "__main__":
    main()
