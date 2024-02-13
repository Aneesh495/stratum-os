#!/usr/bin/env python3
"""
scripts/test_process_ipc.py - Automated tests for Phase P08 Processes, Threads, and IPC (Gates A04, A05).
Verifies:
- Process lifecycle: fork, exit, waitpid, status code propagation
- Virtual memory COW isolation between parent and child
- IPC anonymous pipe creation, blocking read/write, and EOF detection
- Readiness notifications via poll()
- File descriptor table duplication and dup2() redirection
- Multi-core SMP concurrency across 1, 2, 4, 8 vCPUs
"""

import os
import subprocess
import sys
import time

QEMU_BIN = "/opt/homebrew/bin/qemu-system-x86_64"
OVMF_BIN = "/opt/homebrew/Cellar/qemu/11.1.1/share/qemu/edk2-x86_64-code.fd"
BOOT_IMG = "build/images/stratum-boot.img"


def run_ipc_test(cpus):
    print(f"Testing Processes, Threads & IPC ({cpus} vCPUs)... ", end="", flush=True)
    log_path = f"build/serial_ipc_{cpus}.log"
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
    pipe_created = False
    fork_ok = False
    cow_ok = False
    child_write_ok = False
    poll_ok = False
    read_msg_ok = False
    pipe_eof_ok = False
    waitpid_ok = False
    dup2_ok = False
    exit_42_ok = False
    p08_pass_ok = False

    while time.time() - start < 45:
        if os.path.exists(log_path):
            with open(log_path, "r", errors="ignore") as f:
                content = f.read()
                if "Anonymous pipe created successfully" in content:
                    pipe_created = True
                if "Forking child process" in content:
                    fork_ok = True
                if "Copy-On-Write memory isolation verified in parent" in content:
                    cow_ok = True
                if "Child process completed IPC write, exiting with status 77" in content:
                    child_write_ok = True
                if "poll() indicated POLLIN readiness successfully" in content:
                    poll_ok = True
                if "Read message from child via pipe: stratum_pipe_ipc_message" in content:
                    read_msg_ok = True
                if "Pipe EOF detection verified upon writer close" in content:
                    pipe_eof_ok = True
                if "Child process reaped successfully with status 77" in content:
                    waitpid_ok = True
                if "dup2() verified successfully" in content:
                    dup2_ok = True
                if "code 42" in content:
                    exit_42_ok = True
                if "P08 Processes, Threads, and IPC gates verified" in content:
                    p08_pass_ok = True

                if (pipe_created and fork_ok and cow_ok and child_write_ok and
                    poll_ok and read_msg_ok and pipe_eof_ok and waitpid_ok and
                    dup2_ok and exit_42_ok and p08_pass_ok):
                    break
        time.sleep(0.2)

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    all_passed = (pipe_created and fork_ok and cow_ok and child_write_ok and
                  poll_ok and read_msg_ok and pipe_eof_ok and waitpid_ok and
                  dup2_ok and exit_42_ok and p08_pass_ok)

    if all_passed:
        print("PASSED")
        return True
    else:
        print("FAILED")
        print(f"  pipe_created={pipe_created}, fork_ok={fork_ok}, cow_ok={cow_ok}")
        print(f"  child_write_ok={child_write_ok}, poll_ok={poll_ok}, read_msg_ok={read_msg_ok}")
        print(f"  pipe_eof_ok={pipe_eof_ok}, waitpid_ok={waitpid_ok}, dup2_ok={dup2_ok}")
        print(f"  exit_42_ok={exit_42_ok}, p08_pass_ok={p08_pass_ok}")
        if os.path.exists(log_path):
            print("--- Serial Log Tail ---")
            with open(log_path, "r", errors="ignore") as f:
                lines = f.readlines()
                print("".join(lines[-35:]))
        return False


def main():
    print("=== Stratum Phase P08 Verification Test (Gates A04 & A05) ===")
    cpu_configs = [1, 2, 4, 8]
    for cpus in cpu_configs:
        if not run_ipc_test(cpus):
            sys.exit(1)
    print("\n[ALL PASSED] Phase P08 Gates A04 and A05 verified across 1, 2, 4, 8 vCPUs.")
    sys.exit(0)


if __name__ == "__main__":
    main()
