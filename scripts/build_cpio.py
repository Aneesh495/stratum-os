#!/usr/bin/env python3
"""
scripts/build_cpio.py - Deterministic CPIO archive generator for Stratum OS initramfs.
Constructs a bit-for-bit reproducible CPIO newc archive with fixed timestamps,
fixed inodes, and zero host filesystem leakage.
"""

import os
import sys


def make_entry(name, data=b"", mode=0o100755, ino=1):
    name_bytes = name.encode("utf-8") + b"\x00"
    mtime = int(os.environ.get("SOURCE_DATE_EPOCH", 1700000000))
    hdr = f"070701{ino:08x}{mode:08x}{0:08x}{0:08x}{1:08x}{mtime:08x}{len(data):08x}{0:08x}{0:08x}{0:08x}{0:08x}{len(name_bytes):08x}{0:08x}".encode("ascii")
    entry = bytearray(hdr + name_bytes)
    while len(entry) % 4 != 0:
        entry.append(0)
    entry.extend(data)
    while len(entry) % 4 != 0:
        entry.append(0)
    return entry


def build_archive(init_elf_path, out_path):
    with open(init_elf_path, "rb") as f:
        elf_data = f.read()

    archive = bytearray()
    archive.extend(make_entry(".", b"", mode=0o040755, ino=1))
    archive.extend(make_entry("bin", b"", mode=0o040755, ino=2))
    archive.extend(make_entry("bin/init", elf_data, mode=0o100755, ino=3))
    archive.extend(make_entry("TRAILER!!!", b"", mode=0, ino=0))

    while len(archive) % 512 != 0:
        archive.append(0)

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(archive)


def main():
    if len(sys.argv) < 3:
        print("Usage: build_cpio.py <input_elf> <output_cpio>")
        return 1
    build_archive(sys.argv[1], sys.argv[2])
    return 0


if __name__ == "__main__":
    sys.exit(main())
