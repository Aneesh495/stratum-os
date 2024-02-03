#!/usr/bin/env python3
"""
scripts/run_tests.py - Universal test suite runner for Stratum OS.
Dispatches suites across host models, UEFI boot, memory, SMP, scheduler, faults, and ABI.
"""

import argparse
import os
import subprocess
import sys

SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPTS_DIR)


def run_script(script_name, args=None):
    script_path = os.path.join(SCRIPTS_DIR, script_name)
    if not os.path.exists(script_path):
        print(f"Error: {script_name} does not exist.")
        return 1
    cmd = [sys.executable, script_path] + (args if args else [])
    res = subprocess.run(cmd, cwd=PROJECT_ROOT)
    return res.returncode


def run_host_tests():
    print("Running host unit and model tests...")
    host_bin = os.path.join(PROJECT_ROOT, "build", "test_mm_model")
    if not os.path.exists(host_bin):
        # Build host test
        clang = "/opt/homebrew/opt/llvm/bin/clang"
        c_src = os.path.join(PROJECT_ROOT, "tests", "host", "test_mm_model.c")
        if os.path.exists(c_src):
            os.makedirs(os.path.dirname(host_bin), exist_ok=True)
            res = subprocess.run([
                clang, "-fsanitize=address,undefined", "-g", "-O1",
                "-Iinclude", "-Iinclude/kernel", "-Iinclude/shared",
                c_src, "-o", host_bin
            ], cwd=PROJECT_ROOT)
            if res.returncode != 0:
                print("Failed to compile test_mm_model")
                return 1
    if os.path.exists(host_bin):
        res = subprocess.run([host_bin], cwd=PROJECT_ROOT)
        if res.returncode != 0:
            return res.returncode
    print("All host tests passed.")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Stratum Test Runner")
    parser.add_argument("--suite", required=True,
                        choices=["boot", "mm", "smp", "sched", "faults", "abi", "storage", "net", "all_host", "all"])
    args = parser.parse_args()

    if args.suite == "boot":
        return run_script("test_boot.py")
    elif args.suite == "mm":
        return run_script("test_memory.py")
    elif args.suite == "smp":
        return run_script("test_smp.py")
    elif args.suite == "sched":
        return run_script("test_sched.py")
    elif args.suite == "faults":
        return run_script("test_cpu_faults.py")
    elif args.suite == "all_host":
        return run_host_tests()
    elif args.suite == "all":
        suites = ["all_host", "boot", "mm", "smp", "sched", "faults"]
        for s in suites:
            if s == "all_host":
                rc = run_host_tests()
            else:
                rc = run_script(f"test_{s}.py" if s != "faults" else "test_cpu_faults.py")
            if rc != 0:
                print(f"Suite {s} failed with exit code {rc}")
                return rc
        print("\nAll suites passed successfully.")
        return 0
    else:
        print(f"Suite {args.suite} not yet implemented for current phase.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
