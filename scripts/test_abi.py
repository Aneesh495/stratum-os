#!/usr/bin/env python3
"""
scripts/test_abi.py - Automated tests for Phase P07 User ABI, ELF Loading, and Ring 3.
Verifies safe usercopy fault recovery, ELF64 loading, Ring 3 entry,
and system call dispatch (SYS_write, SYS_getpid, SYS_nanosleep, SYS_exit) across vCPUs.
"""

import os
import subprocess
import sys
import time

QEMU_BIN = "/opt/homebrew/bin/qemu-system-x86_64"
OVMF_BIN = "/opt/homebrew/Cellar/qemu/11.1.1/share/qemu/edk2-x86_64-code.fd"
BOOT_IMG = "build/images/stratum-boot.img"


def test_abi_cpu_profile(cpus):
    print(f"Testing User ABI & Ring 3 ({cpus} vCPUs)... ", end="", flush=True)
    log_path = f"build/serial_abi_{cpus}.log"
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
        "-display", "none",
        "-serial", f"file:{log_path}"
    ]

    proc = subprocess.Popen(cmd)
    start = time.time()
    usercopy_ok = False
    elf_ok = False
    ring3_ok = False
    syscall_write_ok = False
    exit_42_ok = False
    p07_pass_ok = False

    while time.time() - start < 10:
        if os.path.exists(log_path):
            with open(log_path, "r", errors="ignore") as f:
                content = f.read()
                if "safe usercopy fault recovery verified" in content:
                    usercopy_ok = True
                if "user ELF64 loader verified" in content:
                    elf_ok = True
                if "Launching userland process in Ring 3" in content:
                    ring3_ok = True
                if "Stratum native userland program running in Ring 3" in content:
                    syscall_write_ok = True
                if "Process exited with code 42" in content:
                    exit_42_ok = True
                if "P07 userland Ring 3 execution & syscall verification passed successfully" in content:
                    p07_pass_ok = True
                    break
        if proc.poll() is not None:
            break
        time.sleep(0.1)

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    if usercopy_ok and elf_ok and ring3_ok and syscall_write_ok and exit_42_ok and p07_pass_ok:
        print(f"[PASS] ({cpus} vCPUs, Ring 3 syscalls verified)")
        return True
    else:
        print("[FAIL]")
        if os.path.exists(log_path):
            with open(log_path, "r", errors="ignore") as f:
                lines = f.readlines()
                print("".join(lines[-30:]))
        return False


def main():
    print("Running P07 User ABI & Ring 3 Verification Suite")
    print("================================================")
    profiles = [1, 2, 4, 8]
    all_ok = True

    for c in profiles:
        ok = test_abi_cpu_profile(c)
        if not ok:
            all_ok = False
            break

    if all_ok:
        print("\nAll P07 User ABI Acceptance Tests Passed.")
        return 0
    else:
        print("\nP07 User ABI Tests Failed.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
