#!/usr/bin/env python3
"""
scripts/test_cpu_faults.py - Automated tests for P03 CPU state and exception dispatch.
Verifies normal boot reaches idle loop and deliberate fault produces decoded interrupt frame.
"""

import os
import subprocess
import sys
import time

QEMU_BIN = "/opt/homebrew/bin/qemu-system-x86_64"
OVMF_BIN = "/opt/homebrew/Cellar/qemu/11.1.1/share/qemu/edk2-x86_64-code.fd"
BOOT_IMG = "build/images/stratum-boot.img"
MCOPY = "/opt/homebrew/bin/mcopy"
MDEL = "/opt/homebrew/bin/mdel"


def test_normal_boot():
    print("Testing normal boot reaches kernel idle loop... ", end="", flush=True)
    # Ensure no cmdline.txt is on disk
    subprocess.run([MDEL, "-i", f"{BOOT_IMG}@@1M", "::/cmdline.txt"], capture_output=True)

    cmd = [
        QEMU_BIN,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64,+nx,+apic",
        "-smp", "2",
        "-m", "256M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_BIN}",
        "-drive", f"file={BOOT_IMG},if=none,id=bootdisk,format=raw",
        "-device", "ide-hd,drive=bootdisk,bootindex=1",
        "-display", "none",
        "-serial", "stdio"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    start = time.time()
    ok = False
    output = []

    while time.time() - start < 15:
        line = proc.stdout.readline()
        if line:
            output.append(line)
            if "Entering kernel loop" in line:
                ok = True
                break
        elif proc.poll() is not None:
            break

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    if ok:
        print("[PASS]")
    else:
        print("[FAIL]")
        print("".join(output))
    return ok


def test_deliberate_fault():
    print("Testing deliberate CPU fault (#UD) yields decoded frame... ", end="", flush=True)
    with open("/tmp/cmdline.txt", "w") as f:
        f.write("fault=ud2\n")

    subprocess.run([MCOPY, "-o", "-i", f"{BOOT_IMG}@@1M", "/tmp/cmdline.txt", "::/cmdline.txt"], check=True)

    cmd = [
        QEMU_BIN,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64,+nx,+apic",
        "-smp", "2",
        "-m", "256M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_BIN}",
        "-drive", f"file={BOOT_IMG},if=none,id=bootdisk,format=raw",
        "-device", "ide-hd,drive=bootdisk,bootindex=1",
        "-display", "none",
        "-serial", "stdio"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    start = time.time()
    found_frame = False
    found_panic = False
    output = []

    while time.time() - start < 15:
        line = proc.stdout.readline()
        if line:
            output.append(line)
            if "INTERRUPT FRAME DUMP" in line:
                found_frame = True
            if "Invalid Opcode (#UD)" in line:
                found_panic = True
            if "System halted" in line:
                break
        elif proc.poll() is not None:
            break

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    # Clean up cmdline.txt
    subprocess.run([MDEL, "-i", f"{BOOT_IMG}@@1M", "::/cmdline.txt"], capture_output=True)

    if found_frame and found_panic:
        print("[PASS] (decoded vector 6 #UD frame confirmed)")
        return True
    else:
        print("[FAIL]")
        print("".join(output))
        return False


def main():
    print("Running P03 CPU State & Trap Tests")
    print("==================================")
    ok1 = test_normal_boot()
    ok2 = test_deliberate_fault()

    if ok1 and ok2:
        print("\nAll P03 CPU and Diagnostic Tests Passed.")
        return 0
    else:
        print("\nP03 Tests Failed.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
