#!/usr/bin/env python3
"""
scripts/test_boot.py - Automated boot validation and malformed image negative tests.
Exercises 1, 2, 4, 8 CPUs, 64M, 256M, 1G profiles, and malformed ELF negative cases.
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

QEMU_BIN = "/opt/homebrew/bin/qemu-system-x86_64"
OVMF_BIN = "/opt/homebrew/Cellar/qemu/11.1.1/share/qemu/edk2-x86_64-code.fd"
BOOT_IMG = "build/images/stratum-boot.img"


def run_guest(cpus, mem, timeout=12):
    cmd = [
        QEMU_BIN,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64,+nx,+apic",
        "-smp", str(cpus),
        "-m", mem,
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_BIN}",
        "-drive", f"file={BOOT_IMG},if=none,id=bootdisk,format=raw",
        "-device", "ide-hd,drive=bootdisk,bootindex=1",
        "-display", "none",
        "-serial", "stdio"
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    start = time.time()
    reached_idle = False
    output = []

    while time.time() - start < timeout:
        line = proc.stdout.readline()
        if line:
            output.append(line)
            if "Entering kernel idle loop" in line:
                reached_idle = True
                break
        elif proc.poll() is not None:
            break

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    return reached_idle, "".join(output)


def test_profiles():
    print("Running Multi-Profile Boot Tests...")
    profiles = [
        (1, "256M"),
        (2, "256M"),
        (4, "256M"),
        (8, "256M"),
        (4, "1G"),
        (1, "64M"),
    ]

    all_passed = True
    for cpus, mem in profiles:
        print(f"Testing profile: {cpus} CPU(s), {mem} RAM... ", end="", flush=True)
        ok, out = run_guest(cpus, mem, timeout=15)
        if ok:
            print("[PASS]")
        else:
            print("[FAIL]")
            print(f"Output:\n{out}")
            all_passed = False

    return all_passed


def test_malformed_images():
    print("\nRunning Malformed Loader Negative Tests...")
    mcopy = "/opt/homebrew/bin/mcopy"
    mformat = "/opt/homebrew/bin/mformat"
    mmd = "/opt/homebrew/bin/mmd"

    # Read original kernel bytes
    with open("build/stratum.elf", "rb") as f:
        orig_kernel = bytearray(f.read())

    mutations = [
        ("bad_magic", orig_kernel[:0] + b"\x7fBAD" + orig_kernel[4:]),
        ("wrong_class_32bit", orig_kernel[:4] + b"\x01" + orig_kernel[5:]),
        ("wrong_endian_big", orig_kernel[:5] + b"\x02" + orig_kernel[6:]),
        ("wrong_machine_arm", orig_kernel[:18] + struct.pack("<H", 183) + orig_kernel[20:]),
        ("truncated_header", orig_kernel[:32]),
        ("corrupt_phoff", orig_kernel[:32] + struct.pack("<Q", 0xFFFFFFFFFFFFFF00) + orig_kernel[40:]),
        ("zero_phnum", orig_kernel[:56] + struct.pack("<H", 0) + orig_kernel[58:]),
    ]

    passed_mutations = 0
    with tempfile.TemporaryDirectory() as tmpdir:
        test_img = os.path.join(tmpdir, "mutant_boot.img")

        for name, mutant_data in mutations:
            print(f"Testing negative case: {name}... ", end="", flush=True)

            # Build mutant image
            size_bytes = 64 * 1024 * 1024
            total_sectors = size_bytes // 512
            start_lba = 2048
            sector_count = total_sectors - start_lba

            with open(test_img, "wb") as f:
                f.truncate(size_bytes)

            with open(test_img, "r+b") as f:
                f.seek(446)
                entry = struct.pack("<BBBBBBBBII", 0x80, 0, 2, 0, 0xEF, 255, 255, 255, start_lba, sector_count)
                f.write(entry)
                f.seek(510)
                f.write(b"\x55\xAA")

            part_target = f"{test_img}@@1M"
            subprocess.run([mformat, "-i", part_target, "-F", "::"], capture_output=True, check=True)
            subprocess.run([mmd, "-i", part_target, "::/EFI"], capture_output=True, check=True)
            subprocess.run([mmd, "-i", part_target, "::/EFI/BOOT"], capture_output=True, check=True)
            subprocess.run([mcopy, "-i", part_target, "build/BOOTX64.EFI", "::/EFI/BOOT/BOOTX64.EFI"], capture_output=True, check=True)

            mutant_elf = os.path.join(tmpdir, "mutant.elf")
            with open(mutant_elf, "wb") as f:
                f.write(mutant_data)

            subprocess.run([mcopy, "-i", part_target, mutant_elf, "::/stratum.elf"], capture_output=True, check=True)

            # Boot in QEMU and ensure loader reports error and rejects invalid kernel
            cmd = [
                QEMU_BIN,
                "-machine", "q35,accel=tcg",
                "-cpu", "qemu64,+nx,+apic",
                "-smp", "1",
                "-m", "256M",
                "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_BIN}",
                "-drive", f"file={test_img},if=none,id=bootdisk,format=raw",
                "-device", "ide-hd,drive=bootdisk,bootindex=1",
                "-display", "none",
                "-serial", "stdio"
            ]

            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            start = time.time()
            observed_rejection = False
            output = []

            while time.time() - start < 8:
                line = proc.stdout.readline()
                if line:
                    output.append(line)
                    if "ERROR:" in line or "validation failed" in line or "Access Denied" in line:
                        observed_rejection = True
                        break
                elif proc.poll() is not None:
                    break

            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()

            if observed_rejection:
                print("[PASS] (safely rejected by loader)")
                passed_mutations += 1
            else:
                print(f"[FAIL] (rejection not observed)")
                print("".join(output))

    print(f"\nNegative tests: {passed_mutations}/{len(mutations)} safely rejected.")
    return passed_mutations == len(mutations)


def main():
    ok_profiles = test_profiles()
    ok_neg = test_malformed_images()

    if ok_profiles and ok_neg:
        print("\nAll Boot and Loader Acceptance Tests Passed.")
        return 0
    else:
        print("\nBoot Acceptance Tests Failed.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
