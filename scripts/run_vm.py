#!/usr/bin/env python3
"""
scripts/run_vm.py - Launch Stratum OS in QEMU with specified profile.
"""

import argparse
import os
import signal
import subprocess
import sys
import time

OVMF_DEFAULT = "/opt/homebrew/Cellar/qemu/11.1.1/share/qemu/edk2-x86_64-code.fd"
QEMU_BIN = "/opt/homebrew/bin/qemu-system-x86_64"


def main():
    parser = argparse.ArgumentParser(description="Run Stratum in QEMU")
    parser.add_argument("--boot-img", required=True, help="Path to boot disk image")
    parser.add_argument("--data-img", required=False, help="Path to data disk image")
    parser.add_argument("--ovmf", default=OVMF_DEFAULT, help="Path to OVMF code.fd")
    parser.add_argument("--cpus", type=int, default=4, help="Number of virtual CPUs (1, 2, 4, 8)")
    parser.add_argument("--mem", default="256M", help="RAM size (e.g. 64M, 256M, 1G)")
    parser.add_argument("--serial", default="stdio", help="Serial device (stdio, null, file:path)")
    parser.add_argument("--gdb", type=int, default=0, help="Wait for GDB connection on port")
    parser.add_argument("--timeout", type=float, default=0, help="Timeout in seconds before terminating VM")
    parser.add_argument("--capture-output", action="store_true", help="Capture and print serial output")
    parser.add_argument("--headless", action="store_true", help="Disable display window")

    args = parser.parse_args()

    cmd = [
        QEMU_BIN if os.path.exists(QEMU_BIN) else "qemu-system-x86_64",
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64,+nx,+apic",
        "-smp", str(args.cpus),
        "-m", args.mem,
        "-drive", f"if=pflash,format=raw,readonly=on,file={args.ovmf}",
        "-drive", f"file={args.boot_img},if=none,id=bootdisk,format=raw",
        "-device", "ide-hd,drive=bootdisk,bootindex=1",
    ]

    if args.data_img and os.path.exists(args.data_img):
        cmd += [
            "-drive", f"file={args.data_img},if=none,id=datadisk,format=raw",
            "-device", "virtio-blk-pci,drive=datadisk,disable-legacy=on",
        ]

    cmd += [
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0,disable-legacy=on",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
    ]

    if args.headless or args.capture_output or args.timeout > 0:
        cmd += ["-display", "none"]

    if args.serial == "stdio":
        cmd += ["-serial", "stdio"]
    elif args.serial.startswith("file:"):
        cmd += ["-serial", args.serial]
    else:
        cmd += ["-serial", "mon:stdio"]

    if args.gdb > 0:
        cmd += ["-gdb", f"tcp::{args.gdb}", "-S"]

    if args.capture_output:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        start_time = time.time()
        output_lines = []
        try:
            while True:
                line = proc.stdout.readline()
                if line:
                    output_lines.append(line)
                    print(line, end="", flush=True)
                    if "Entering kernel idle loop" in line or "System halted" in line:
                        time.sleep(0.5)
                        proc.terminate()
                        break
                elif proc.poll() is not None:
                    break
                if args.timeout > 0 and (time.time() - start_time) > args.timeout:
                    proc.kill()
                    print("\n[VM TIMEOUT]")
                    break
        except KeyboardInterrupt:
            proc.terminate()
        proc.wait()
        return proc.returncode
    elif args.timeout > 0:
        proc = subprocess.Popen(cmd)
        try:
            proc.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait()
        return 0
    else:
        return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
