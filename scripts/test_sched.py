#!/usr/bin/env python3
"""
scripts/test_sched.py - Automated tests for Phase P06 Preemptive SMP Scheduler.
Verifies thread scheduling, sleep/wake, preemption, and work stealing across 1, 2, 4, and 8 vCPUs.
"""

import os
import subprocess
import sys
import time

QEMU_BIN = "/opt/homebrew/bin/qemu-system-x86_64"
OVMF_BIN = "/opt/homebrew/share/qemu/edk2-x86_64-code.fd"
BOOT_IMG = "build/images/stratum-boot.img"


import shutil


def test_sched_cpu_profile(cpus):
    print(f"Testing SMP Scheduler ({cpus} vCPUs)... ", end="", flush=True)
    fresh_data = f"build/images/stratum-data-sched-{cpus}.img"
    shutil.copyfile("build/images/stratum-data.img", fresh_data)
    cmd = [
        QEMU_BIN,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64,+nx,+apic",
        "-smp", str(cpus),
        "-m", "256M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_BIN}",
        "-drive", f"file={BOOT_IMG},if=none,id=bootdisk,format=raw",
        "-device", "ide-hd,drive=bootdisk,bootindex=1",
        "-drive", f"file={fresh_data},if=none,id=datadisk,format=raw",
        "-device", "virtio-blk-pci,drive=datadisk",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-display", "none",
        "-serial", "stdio"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    import fcntl
    flags = fcntl.fcntl(proc.stdout, fcntl.F_GETFL)
    fcntl.fcntl(proc.stdout, fcntl.F_SETFL, flags | os.O_NONBLOCK)

    start = time.time()
    sched_init_ok = False
    workers_ok = False
    sleeper_ok = False
    sched_pass_ok = False
    output = []

    while time.time() - start < 30:
        try:
            chunk = proc.stdout.read(4096)
            if chunk:
                output.append(chunk)
                content = "".join(output)
                if "Preemptive SMP Scheduler initialized" in content:
                    sched_init_ok = True
                if "Worker 1 finished" in content or "Worker 2 finished" in content or "Worker 3 finished" in content:
                    workers_ok = True
                if "Sleeper 4 woke up" in content:
                    sleeper_ok = True
                if "P06 scheduler verification passed successfully" in content:
                    sched_pass_ok = True
                if sched_init_ok and workers_ok and sleeper_ok and sched_pass_ok:
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

    if sched_init_ok and workers_ok and sleeper_ok and sched_pass_ok:
        print(f"[PASS] ({cpus} vCPUs, threads preempted/slept/woken)")
        return True
    else:
        print("[FAIL]")
        print("".join(output))
        return False


def main():
    print("Running P06 Preemptive SMP Scheduler Suite (Gate A03)")
    print("====================================================")
    profiles = [1, 2, 4, 8]
    all_ok = True

    for c in profiles:
        ok = test_sched_cpu_profile(c)
        if not ok:
            all_ok = False
            break

    if all_ok:
        print("\nAll P06 Scheduler Acceptance Tests Passed.")
        return 0
    else:
        print("\nP06 Scheduler Tests Failed.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
