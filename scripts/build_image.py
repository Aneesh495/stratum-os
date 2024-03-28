#!/usr/bin/env python3
"""
scripts/build_image.py - Constructs bootable EFI System Partition image and StrataFS data disk.
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys

BOOT_IMAGE_SIZE_MIB = 64
DATA_IMAGE_SIZE_MIB = 64

os.environ["SOURCE_DATE_EPOCH"] = "1700000000"


def run_cmd(cmd):
    env = os.environ.copy()
    env["SOURCE_DATE_EPOCH"] = "1700000000"
    res = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if res.returncode != 0:
        print(f"Command failed: {' '.join(cmd)}")
        print(f"stdout: {res.stdout}")
        print(f"stderr: {res.stderr}")
        sys.exit(1)
    return res.stdout


def find_tool(candidates):
    for c in candidates:
        if os.path.isabs(c):
            if os.path.exists(c) and os.access(c, os.X_OK):
                return c
        else:
            p = shutil.which(c)
            if p:
                return p
    return candidates[0]


def sha256_file(filepath):
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def build_boot_disk(out_path, loader_path, kernel_path, initramfs_path):
    mformat = find_tool(["/opt/homebrew/bin/mformat", "mformat"])
    mmd = find_tool(["/opt/homebrew/bin/mmd", "mmd"])
    mcopy = find_tool(["/opt/homebrew/bin/mcopy", "mcopy"])

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)

    # 1. Create raw zeroed image file
    total_bytes = BOOT_IMAGE_SIZE_MIB * 1024 * 1024
    with open(out_path, "wb") as f:
        f.truncate(total_bytes)

    # 2. Write MBR partition table (Partition 1: type 0xEF, start LBA 2048)
    import struct
    total_sectors = total_bytes // 512
    start_lba = 2048
    sector_count = total_sectors - start_lba
    with open(out_path, "r+b") as f:
        f.seek(446)
        entry = struct.pack("<BBBBBBBBII", 0x80, 0, 2, 0, 0xEF, 255, 255, 255, start_lba, sector_count)
        f.write(entry)
        f.seek(510)
        f.write(b"\x55\xAA")

    part_target = f"{out_path}@@1M"

    # 3. Format as FAT32 ESP with deterministic volume serial
    run_cmd([mformat, "-i", part_target, "-F", "-N", "12345678", "::"])

    # 4. Create EFI directories
    run_cmd([mmd, "-i", part_target, "::/EFI"])
    run_cmd([mmd, "-i", part_target, "::/EFI/BOOT"])

    # Deterministic file timestamping (epoch 1700000000)
    fixed_time = int(os.environ.get("SOURCE_DATE_EPOCH", 1700000000))
    if os.path.exists(loader_path):
        os.utime(loader_path, (fixed_time, fixed_time))
    if os.path.exists(kernel_path):
        os.utime(kernel_path, (fixed_time, fixed_time))

    # 5. Copy bootloader and kernel
    run_cmd([mcopy, "-m", "-i", part_target, loader_path, "::/EFI/BOOT/BOOTX64.EFI"])
    run_cmd([mcopy, "-m", "-i", part_target, kernel_path, "::/stratum.elf"])

    # 6. Add startup.nsh fallback for UEFI shell profiles
    import tempfile
    with tempfile.NamedTemporaryFile("w", delete=False) as tf:
        tf.write("\\EFI\\BOOT\\BOOTX64.EFI\r\n")
        tf_name = tf.name
    os.utime(tf_name, (fixed_time, fixed_time))
    run_cmd([mcopy, "-m", "-o", "-i", part_target, tf_name, "::/startup.nsh"])
    os.remove(tf_name)

    # 7. Copy initramfs if present
    if initramfs_path and os.path.exists(initramfs_path):
        os.utime(initramfs_path, (fixed_time, fixed_time))
        run_cmd([mcopy, "-m", "-i", part_target, initramfs_path, "::/initramfs.cpio"])

    print(f"[OK] Boot image created: {out_path} ({BOOT_IMAGE_SIZE_MIB} MiB partitioned MBR/ESP)")
    print(f"     SHA-256: {sha256_file(out_path)}")


def build_data_disk(out_path):
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as f:
        f.truncate(DATA_IMAGE_SIZE_MIB * 1024 * 1024)

    print(f"[OK] Data image created: {out_path} ({DATA_IMAGE_SIZE_MIB} MiB)")
    print(f"     SHA-256: {sha256_file(out_path)}")


def main():
    parser = argparse.ArgumentParser(description="Build Stratum disk images")
    parser.add_argument("--boot-img", required=True, help="Output boot disk image path")
    parser.add_argument("--data-img", required=True, help="Output data disk image path")
    parser.add_argument("--loader", required=True, help="Input BOOTX64.EFI path")
    parser.add_argument("--kernel", required=True, help="Input stratum.elf path")
    parser.add_argument("--initramfs", required=False, help="Input initramfs.cpio path")

    args = parser.parse_args()

    build_boot_disk(args.boot_img, args.loader, args.kernel, args.initramfs)
    build_data_disk(args.data_img)
    return 0


if __name__ == "__main__":
    sys.exit(main())
