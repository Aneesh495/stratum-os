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
OVMF_BIN = "/opt/homebrew/share/qemu/edk2-x86_64-code.fd"
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
        "-serial", "stdio"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    start = time.time()

    full_output = []
    while time.time() - start < 30:
        line = proc.stdout.readline()
        if not line and proc.poll() is not None:
            break
        if line:
            full_output.append(line)
            if "P08 Processes, Threads, and IPC gates verified" in line:
                break

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    content = "".join(full_output)
    with open(log_path, "w") as f:
        f.write(content)

    pipe_created = "Anonymous pipe created successfully" in content
    fork_ok = "Forking child process" in content
    cow_ok = "Copy-On-Write memory isolation verified in parent" in content
    child_write_ok = "Child process completed IPC write, exiting with status 77" in content
    poll_ok = "poll() indicated POLLIN readiness successfully" in content
    read_msg_ok = "Read message from child via pipe: stratum_pipe_ipc_message" in content
    pipe_eof_ok = "Pipe EOF detection verified upon writer close" in content
    waitpid_ok = "Child process reaped successfully with status 77" in content
    dup2_ok = "dup2() verified successfully" in content
    exit_42_ok = "code 42" in content
    p08_pass_ok = "P08 Processes, Threads, and IPC gates verified" in content

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
