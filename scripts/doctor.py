#!/usr/bin/env python3
"""
scripts/doctor.py - Toolchain, firmware, and host verification for Stratum OS.
"""

import hashlib
import os
import shutil
import subprocess
import sys

REQUIRED_TOOLS = [
    ("Clang", ["/opt/homebrew/opt/llvm/bin/clang", "clang"], "--version"),
    ("LLD Linker", ["/opt/homebrew/opt/llvm/bin/ld.lld", "ld.lld"], "--version"),
    ("NASM Assembler", ["/opt/homebrew/bin/nasm", "nasm"], "-v"),
    ("llvm-objcopy", ["/opt/homebrew/opt/llvm/bin/llvm-objcopy", "llvm-objcopy"], "--version"),
    ("mformat", ["/opt/homebrew/bin/mformat", "mformat"], "--version"),
    ("mcopy", ["/opt/homebrew/bin/mcopy", "mcopy"], "--version"),
    ("QEMU x86-64", ["/opt/homebrew/bin/qemu-system-x86_64", "qemu-system-x86_64"], "--version"),
]

OVMF_PATHS = [
    "/opt/homebrew/share/qemu/edk2-x86_64-code.fd",
    "/usr/share/OVMF/OVMF_CODE.fd",
    "/usr/share/qemu/edk2-x86_64-code.fd",
]

EXPECTED_OVMF_SHA256 = "33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a"


def find_tool(candidates):
    for c in candidates:
        if os.path.isabs(c):
            if os.path.exists(c) and os.access(c, os.X_OK):
                return c
        else:
            p = shutil.which(c)
            if p:
                return p
    return None


def main():
    print("Stratum OS Environment Doctor")
    print("==============================")
    all_ok = True

    for name, candidates, flag in REQUIRED_TOOLS:
        path = find_tool(candidates)
        if not path:
            print(f"[FAIL] {name}: not found (tried {candidates})")
            all_ok = False
        else:
            try:
                res = subprocess.run([path, flag], capture_output=True, text=True, check=True)
                line = res.stdout.splitlines()[0] if res.stdout else res.stderr.splitlines()[0]
                print(f"[OK]   {name}: {path} ({line.strip()})")
            except Exception as e:
                print(f"[FAIL] {name}: {path} error executing {flag}: {e}")
                all_ok = False

    ovmf_found = None
    for p in OVMF_PATHS:
        if os.path.exists(p):
            ovmf_found = p
            break

    if not ovmf_found:
        print(f"[FAIL] OVMF Firmware: not found in {OVMF_PATHS}")
        all_ok = False
    else:
        with open(ovmf_found, "rb") as f:
            h = hashlib.sha256(f.read()).hexdigest()
        if h == EXPECTED_OVMF_SHA256:
            print(f"[OK]   OVMF Firmware: {ovmf_found} (hash match: {h[:16]}...)")
        else:
            print(f"[WARN] OVMF Firmware: {ovmf_found} hash {h} != expected {EXPECTED_OVMF_SHA256}")

    if all_ok:
        print("\nAll required toolchain and platform dependencies verified.")
        return 0
    else:
        print("\nOne or more dependencies failed verification.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
