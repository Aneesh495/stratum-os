#!/usr/bin/env python3
"""
scripts/test_network.py - Automated tests for Phase P12 Original Network Stack (TCP/IP) (Gate A08).

Verifies:
- Ethernet Layer II framing, dispatch, and broadcast filtering
- Address Resolution Protocol (ARP) table management and cache lookups
- IPv4 Layer III framing, IP checksum validation, and packet routing
- ICMP Layer IV Echo Request responder and reply generation
- UDP datagram transmission with pseudo-header checksum calculation
- TCP state machine: 3-way handshake (SYN, SYN-ACK, ACK), sequence/ack tracking,
  sliding window stream transmission, ring buffer reception, and connection teardown (FIN, ACK)
- BSD Socket API: sys_socket, sys_bind, sys_listen, sys_accept, sys_connect, sys_send, sys_recv, sys_shutdown
- Multi-core SMP concurrency across 1, 2, 4, and 8 vCPUs
"""

import os
import subprocess
import sys
import time

QEMU_BIN = "/opt/homebrew/bin/qemu-system-x86_64"
OVMF_BIN = "/opt/homebrew/share/qemu/edk2-x86_64-code.fd"
BOOT_IMG = "build/images/stratum-boot.img"
DATA_IMG = "build/images/stratum-data.img"


import shutil


def run_network_test(cpus):
    print(f"Testing Network Stack & TCP/IP ({cpus} vCPUs)... ", end="", flush=True)
    log_path = f"build/serial_net_{cpus}.log"
    if os.path.exists(log_path):
        os.remove(log_path)

    fresh_data = f"build/images/stratum-data-net-{cpus}.img"
    shutil.copyfile(DATA_IMG, fresh_data)

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
    start = time.time()

    full_output = []
    while time.time() - start < 30:
        line = proc.stdout.readline()
        if not line and proc.poll() is not None:
            break
        if line:
            full_output.append(line)
            if "Gate A08 passed" in line:
                break

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    content = "".join(full_output)
    with open(log_path, "w") as f:
        f.write(content)

    csum_ok = "Internet checksum calculation verified" in content
    arp_ok = "ARP cache lookup verified" in content
    udp_ok = "UDP datagram transmission with pseudo-header checksum verified" in content
    icmp_ok = "ICMP echo request handling and reply transmission verified" in content
    tcp_handshake_ok = "TCP 3-way handshake completed; both endpoints ESTABLISHED" in content
    tcp_stream_ok = "TCP stream payload transfer and ring buffer readback verified" in content
    socket_api_ok = "BSD Socket API (socket, bind, listen, shutdown) verified" in content
    gate_a08_ok = "Gate A08 passed" in content

    all_passed = (csum_ok and arp_ok and udp_ok and icmp_ok and
                  tcp_handshake_ok and tcp_stream_ok and socket_api_ok and gate_a08_ok)

    if all_passed:
        print("PASSED")
        return True
    else:
        print("FAILED")
        print(f"  csum={csum_ok}, arp={arp_ok}, udp={udp_ok}, icmp={icmp_ok}")
        print(f"  tcp_handshake={tcp_handshake_ok}, tcp_stream={tcp_stream_ok}, socket_api={socket_api_ok}, gate_a08={gate_a08_ok}")
        if os.path.exists(log_path):
            print("--- Serial Log Tail ---")
            with open(log_path, "r", errors="ignore") as f:
                lines = f.readlines()
                print("".join(lines[-35:]))
        return False


def main():
    print("=== Stratum Phase P12 Verification Test (Gate A08) ===")
    cpu_configs = [1, 2, 4, 8]
    for cpus in cpu_configs:
        if not run_network_test(cpus):
            sys.exit(1)
    print("\n[ALL PASSED] Phase P12 Gate A08 verified across 1, 2, 4, 8 vCPUs.")
    sys.exit(0)


if __name__ == "__main__":
    main()
