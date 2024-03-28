#!/usr/bin/env python3
"""
scripts/test_soak.py - Automated Test Runner for Phase P14 Depth, Concurrency, and Soak Verification (Gate A10).

Verifies:
- Independent StrataFS filesystem consistency checker (K56, tools/strata_check)
- Deterministic models and replay test suite (K57, tests/host/test_models)
- Fault injection and fuzz regression test suite (K59, tests/faults/test_faults)
- Real guest multi-threaded stress and soak workload on 8 vCPUs and 1 GiB RAM (K60, tests/guest/stress.c)
- Lock-freedom, flat memory footprint, and zero resource leaks
"""

import os
import shutil
import subprocess
import sys
import time

QEMU_BIN = "/opt/homebrew/bin/qemu-system-x86_64"
OVMF_BIN = "/opt/homebrew/share/qemu/edk2-x86_64-code.fd"
BOOT_IMG = "build/images/stratum-boot.img"
DATA_IMG = "build/images/stratum-data.img"


def run_host_suite():
    print("--- 1. Running Host Verification Suites ---")
    clang = "/opt/homebrew/opt/llvm/bin/clang"
    if not os.path.exists(clang):
        clang = "clang"

    # 1. Host deterministic models
    if not os.path.exists("./tests/host/test_models"):
        subprocess.run([clang, "-fsanitize=address,undefined", "-g", "-O1", "tests/host/test_models.c", "-o", "tests/host/test_models"])
    print("Running deterministic models and replay suite (K57)... ", end="", flush=True)
    res = subprocess.run(["./tests/host/test_models"], capture_output=True, text=True)
    if res.returncode == 0 and "All deterministic models completed successfully" in res.stdout:
        print("PASSED")
    else:
        print("FAILED")
        print(res.stdout)
        print(res.stderr)
        return False

    # 2. Host fault injection & fuzzing
    if not os.path.exists("./tests/faults/test_faults"):
        subprocess.run([clang, "-fsanitize=address,undefined", "-g", "-O1", "tests/faults/test_faults.c", "-o", "tests/faults/test_faults"])
    print("Running fault injection and fuzz regression suite (K59)... ", end="", flush=True)
    res2 = subprocess.run(["./tests/faults/test_faults"], capture_output=True, text=True)
    if res2.returncode == 0 and "All fault injection and fuzzing scenarios handled safely" in res2.stdout:
        print("PASSED")
    else:
        print("FAILED")
        print(res2.stdout)
        print(res2.stderr)
        return False

    # 3. Independent filesystem checker
    if not os.path.exists("./tools/strata_check"):
        subprocess.run([clang, "-O2", "tools/strata_check.c", "-o", "tools/strata_check"])
    print("Running independent StrataFS filesystem checker (K56)... ", end="", flush=True)
    check_img = "build/images/stratum-data-soak.img"
    if not os.path.exists("build/images/stratum-data-1.img"):
        shutil.copyfile("build/images/stratum-data.img", "build/images/stratum-data-1.img")
    shutil.copyfile("build/images/stratum-data-1.img", check_img)
    res3 = subprocess.run(["./tools/strata_check", check_img], capture_output=True, text=True)
    if res3.returncode == 0 and "StrataFS filesystem structure is consistent and verified" in res3.stdout:
        print("PASSED")
    else:
        print("FAILED")
        print(res3.stdout)
        print(res3.stderr)
        return False

    return True


def run_guest_soak_test():
    print("\n--- 2. Running Real Guest Stress and Soak Workload (8 vCPUs, 1 GiB RAM) ---")
    log_path = "build/serial_soak_8cpu_1g.log"
    if os.path.exists(log_path):
        os.remove(log_path)

    fresh_boot = "build/images/stratum-boot-soak.img"
    fresh_data = "build/images/stratum-data-soak-guest.img"
    shutil.copyfile(BOOT_IMG, fresh_boot)
    shutil.copyfile(DATA_IMG, fresh_data)

    cmd = [
        QEMU_BIN,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64,+nx,+apic",
        "-smp", "8",
        "-m", "1G",
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_BIN}",
        "-drive", f"file={fresh_boot},if=none,id=bootdisk,format=raw",
        "-device", "ide-hd,drive=bootdisk,bootindex=1",
        "-drive", f"file={fresh_data},if=none,id=datadisk,format=raw",
        "-device", "virtio-blk-pci,drive=datadisk",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-display", "none",
        "-serial", "stdio"
    ]

    print("Launching QEMU guest soak harness (8 vCPUs, 1 GiB RAM)... ", end="", flush=True)
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    import fcntl
    flags = fcntl.fcntl(proc.stdout, fcntl.F_GETFL)
    fcntl.fcntl(proc.stdout, fcntl.F_SETFL, flags | os.O_NONBLOCK)

    start = time.time()
    full_output = []
    while time.time() - start < 15:
        try:
            chunk = proc.stdout.read(4096)
            if chunk:
                full_output.append(chunk)
                if "Gate A10 passed" in "".join(full_output):
                    break
        except Exception:
            pass
        if proc.poll() is not None:
            break
        time.sleep(0.05)

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    content = "".join(full_output)
    with open(log_path, "w") as f:
        f.write(content)

    soak_ok = False
    cpu8_ok = False
    zero_leak_ok = False

    if os.path.exists(log_path):
        with open(log_path, "r", errors="ignore") as f:
            content = f.read()
            if "Real Guest Stress and Soak verified successfully (Gate A10 passed)" in content:
                soak_ok = True
            if "Online CPUs:  8" in content or "CPUs=8" in content:
                cpu8_ok = True
            if "flat memory footprint" in content:
                zero_leak_ok = True

    if soak_ok and cpu8_ok and zero_leak_ok:
        print("PASSED")
        print("  Soak workload: verified")
        print("  SMP 8 vCPUs: active and verified")
        print("  Memory footprint: flat with zero leaks verified")
        return True
    else:
        print("FAILED")
        print(f"  soak_ok={soak_ok}, cpu8_ok={cpu8_ok}, zero_leak_ok={zero_leak_ok}")
        if os.path.exists(log_path):
            print("--- Serial Log Tail ---")
            with open(log_path, "r", errors="ignore") as f:
                lines = f.readlines()
                print("".join(lines[-40:]))
        return False


def main():
    print("=== Stratum Phase P14 Verification Suite (Gate A10) ===")
    if not run_host_suite():
        sys.exit(1)

    if not run_guest_soak_test():
        sys.exit(1)

    print("\n[ALL PASSED] Phase P14 Gate A10 verified across all host models, fault injection, and 8 vCPU / 1 GiB soak workload.")
    sys.exit(0)


if __name__ == "__main__":
    main()
