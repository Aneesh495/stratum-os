#!/usr/bin/env python3
"""
scripts/test_smp.py - Automated tests for Phase P05 SMP Startup & Synchronization.
Verifies SMP initialization and synchronization across 1, 2, 4, and 8 vCPUs.
"""

import os
import subprocess
import sys
import time

QEMU_BIN = "/opt/homebrew/bin/qemu-system-x86_64"
OVMF_BIN = "/opt/homebrew/share/qemu/edk2-x86_64-code.fd"
BOOT_IMG = "build/images/stratum-boot.img"


def test_smp_cpu_profile(cpus):
    print(f"Testing SMP Startup ({cpus} vCPUs)... ", end="", flush=True)
    cmd = [
        QEMU_BIN,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64,+nx,+apic",
        "-smp", str(cpus),
        "-m", "256M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_BIN}",
        "-drive", f"file={BOOT_IMG},if=none,id=bootdisk,format=raw",
        "-device", "ide-hd,drive=bootdisk,bootindex=1",
        "-display", "none",
        "-serial", "stdio"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    start = time.time()
    smp_init_ok = False
    sync_ok = False
    idle_ok = False
    online_count_ok = False
    output = []

    expected_smp_msg = f"{cpus} CPU(s) online"

    while time.time() - start < 15:
        line = proc.stdout.readline()
        if line:
            output.append(line)
            if "Multiprocessor startup complete" in line:
                smp_init_ok = True
                if expected_smp_msg in line:
                    online_count_ok = True
            elif cpus == 1 and "Single processor detected" in line:
                smp_init_ok = True
                online_count_ok = True
            if "P05 SMP synchronization verification passed successfully" in line:
                sync_ok = True
            if "Entering kernel idle loop" in line:
                idle_ok = True
                break
        elif proc.poll() is not None:
            break

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    if smp_init_ok and online_count_ok and sync_ok and idle_ok:
        print(f"[PASS] ({cpus} online, sync barrier verified)")
        return True
    else:
        print("[FAIL]")
        print("".join(output))
        return False


def main():
    print("Running P05 Multiprocessor & Synchronization Suite")
    print("==================================================")
    profiles = [1, 2, 4, 8]
    all_ok = True

    for c in profiles:
        ok = test_smp_cpu_profile(c)
        if not ok:
            all_ok = False
            break

    if all_ok:
        print("\nAll P05 SMP Acceptance Tests Passed.")
        return 0
    else:
        print("\nP05 SMP Tests Failed.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
