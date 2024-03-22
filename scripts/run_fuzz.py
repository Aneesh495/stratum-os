#!/usr/bin/env python3
"""
scripts/run_fuzz.py - Executes differential fuzzing suite for Gate A11.
Compiles tests/fuzz/test_fuzz.c with AddressSanitizer and UndefinedBehaviorSanitizer,
running 2,100,000 executions across all 6 parser/decoder targets.
"""

import os
import subprocess
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUZZ_SRC = os.path.join(PROJECT_ROOT, "tests", "fuzz", "test_fuzz.c")
FUZZ_BIN = os.path.join(PROJECT_ROOT, "build", "test_fuzz")


def main():
    os.makedirs(os.path.dirname(FUZZ_BIN), exist_ok=True)
    clang = "/opt/homebrew/opt/llvm/bin/clang"
    if not os.path.exists(clang):
        clang = "clang"

    print("=== Compiling Stratum Fuzzing Suite with ASan & UBSan ===")
    compile_cmd = [
        clang, "-fsanitize=address,undefined", "-g", "-O2",
        "-Wall", "-Wextra",
        FUZZ_SRC, "-o", FUZZ_BIN
    ]
    res = subprocess.run(compile_cmd, cwd=PROJECT_ROOT)
    if res.returncode != 0:
        print("[FAIL] Failed to compile fuzz suite with sanitizers.")
        return 1

    print("=== Executing 2,100,000 Fuzz Executions Across 6 Targets ===")
    res = subprocess.run([FUZZ_BIN, "350000"], cwd=PROJECT_ROOT)
    if res.returncode != 0:
        print(f"[FAIL] Fuzz suite failed with exit code {res.returncode}")
        return res.returncode

    print("[OK] Gate A11 Fuzzing Verification Passed Cleanly.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
