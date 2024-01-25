#!/usr/bin/env python3
"""
scripts/test_memory.py - Automated tests for Phase P04 Memory Management (Gate A02).
Tests physical allocation, slab object cache, virtual mapping, and host MM stress model.
"""

import os
import subprocess
import sys
import time

QEMU_BIN = "/opt/homebrew/bin/qemu-system-x86_64"
OVMF_BIN = "/opt/homebrew/Cellar/qemu/11.1.1/share/qemu/edk2-x86_64-code.fd"
BOOT_IMG = "build/images/stratum-boot.img"
CLANG_BIN = "/opt/homebrew/opt/llvm/bin/clang"


def test_host_model():
    print("Testing Host MM Reference & Accounting Model (2,000,000 ops)... ", end="", flush=True)
    os.makedirs("build", exist_ok=True)
    compile_cmd = [
        CLANG_BIN,
        "-fsanitize=address,undefined",
        "-Wall", "-Wextra", "-std=c17", "-O2",
        "tests/host/test_mm_model.c",
        "-o", "build/test_mm_model"
    ]
    res = subprocess.run(compile_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("[FAIL] (Compilation error)")
        print(res.stderr)
        return False

    run_res = subprocess.run(["./build/test_mm_model"], capture_output=True, text=True)
    if run_res.returncode == 0 and "Zero memory leakage" in run_res.stdout:
        print("[PASS]")
        return True
    else:
        print("[FAIL]")
        print(run_res.stdout)
        print(run_res.stderr)
        return False


def test_guest_memory():
    print("Testing Guest Memory (PMM, SLAB, VMM 4-Level Paging)... ", end="", flush=True)
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
    pmm_ok = False
    slab_ok = False
    vmm_ok = False
    test_ok = False
    idle_ok = False
    output = []

    while time.time() - start < 15:
        line = proc.stdout.readline()
        if line:
            output.append(line)
            if "Physical memory manager initialized" in line:
                pmm_ok = True
            if "Object cache allocator initialized" in line:
                slab_ok = True
            if "Virtual memory manager initialized with 4-level paging" in line:
                vmm_ok = True
            if "P04 memory verification tests passed successfully" in line:
                test_ok = True
            if "Entering kernel idle loop" in line or "Entering kernel loop" in line:
                idle_ok = True
                break
        elif proc.poll() is not None:
            break

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    if pmm_ok and slab_ok and vmm_ok and test_ok and idle_ok:
        print("[PASS]")
        return True
    else:
        print("[FAIL]")
        print("".join(output))
        return False


def main():
    print("Running P04 Memory Management Acceptance Suite (Gate A02)")
    print("=========================================================")
    ok1 = test_host_model()
    ok2 = test_guest_memory()

    if ok1 and ok2:
        print("\nAll P04 Memory Acceptance Tests Passed.")
        return 0
    else:
        print("\nP04 Memory Tests Failed.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
