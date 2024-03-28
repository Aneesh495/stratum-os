#!/usr/bin/env python3
"""
scripts/run_acceptance.py - Master Acceptance Test Runner for Stratum OS (Gate A12).
Executes the full suite of acceptance workloads from A01 through A12,
constructs two clean isolated builds to verify bit-for-bit reproducibility,
and generates structured evidence logs and hashes in evidence/.
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EVIDENCE_DIR = os.path.join(PROJECT_ROOT, "evidence")
LOGS_DIR = os.path.join(EVIDENCE_DIR, "logs")


def sha256_file(filepath):
    if not os.path.exists(filepath):
        return None
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def run_gate(gate_id, name, cmd):
    print(f"\n=======================================================")
    print(f"  Executing Acceptance Gate {gate_id}: {name}")
    print(f"=======================================================")
    log_file = os.path.join(LOGS_DIR, f"{gate_id.lower()}_{name.lower().replace(' ', '_')}.log")

    start_time = time.time()
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=PROJECT_ROOT)
    elapsed = time.time() - start_time

    with open(log_file, "w", encoding="utf-8") as f:
        f.write(res.stdout)

    status = "PASSED" if res.returncode == 0 else "FAILED"
    print(f"[{status}] Gate {gate_id} completed in {elapsed:.2f}s (Exit code: {res.returncode})")
    if res.returncode != 0:
        print("--- Output Tail ---")
        lines = res.stdout.splitlines()
        for l in lines[-20:]:
            print(f"  {l}")

    return {
        "gate": gate_id,
        "name": name,
        "status": status,
        "exit_code": res.returncode,
        "elapsed_seconds": round(elapsed, 2),
        "log_file": os.path.relpath(log_file, PROJECT_ROOT)
    }


def perform_reproducible_builds():
    print("\n=======================================================")
    print("  Executing Gate A12: Reproducible Dual Isolated Builds")
    print("=======================================================")
    build_a_dir = os.path.join(EVIDENCE_DIR, "build_A")
    build_b_dir = os.path.join(EVIDENCE_DIR, "build_B")
    shutil.rmtree(build_a_dir, ignore_errors=True)
    shutil.rmtree(build_b_dir, ignore_errors=True)
    os.makedirs(build_a_dir, exist_ok=True)
    os.makedirs(build_b_dir, exist_ok=True)

    # Clean build A
    print("Executing Clean Build A...")
    subprocess.run(["make", "clean"], cwd=PROJECT_ROOT, capture_output=True)
    res_a = subprocess.run(["make", "all"], cwd=PROJECT_ROOT, capture_output=True, text=True)
    if res_a.returncode != 0:
        print("[FAIL] Build A failed.")
        return None

    # Copy artifacts A
    art_files = ["BOOTX64.EFI", "stratum.elf", "images/stratum-boot.img", "images/stratum-data.img"]
    hashes_a = {}
    for af in art_files:
        src = os.path.join(PROJECT_ROOT, "build", af)
        dst = os.path.join(build_a_dir, os.path.basename(af))
        shutil.copyfile(src, dst)
        hashes_a[os.path.basename(af)] = sha256_file(dst)

    # Clean build B
    print("Executing Clean Build B...")
    subprocess.run(["make", "clean"], cwd=PROJECT_ROOT, capture_output=True)
    res_b = subprocess.run(["make", "all"], cwd=PROJECT_ROOT, capture_output=True, text=True)
    if res_b.returncode != 0:
        print("[FAIL] Build B failed.")
        return None

    # Copy artifacts B
    hashes_b = {}
    for af in art_files:
        src = os.path.join(PROJECT_ROOT, "build", af)
        dst = os.path.join(build_b_dir, os.path.basename(af))
        shutil.copyfile(src, dst)
        hashes_b[os.path.basename(af)] = sha256_file(dst)

    print("\nArtifact Hash Comparison (Build A vs Build B):")
    all_matched = True
    match_report = {}
    for k in hashes_a:
        match = (hashes_a[k] == hashes_b[k])
        if not match:
            all_matched = False
        print(f"  {k:20s}: {'MATCH' if match else 'MISMATCH'}")
        print(f"    Build A: {hashes_a[k]}")
        print(f"    Build B: {hashes_b[k]}")
        match_report[k] = {
            "build_a_hash": hashes_a[k],
            "build_b_hash": hashes_b[k],
            "match": match
        }

    return {
        "reproducible": all_matched,
        "artifacts": match_report
    }


def main():
    os.makedirs(LOGS_DIR, exist_ok=True)

    print("###############################################################")
    print("  Stratum Operating System: Full Master Acceptance Suite")
    print("###############################################################")

    gates_summary = []

    # A01: Boot and image profiles
    gates_summary.append(run_gate("A01", "Boot and Image Profiles", [sys.executable, "scripts/test_boot.py"]))

    # A02: Memory management
    gates_summary.append(run_gate("A02", "Memory Management and COW", [sys.executable, "scripts/test_memory.py"]))

    # A03: Preemptive scheduler & concurrency
    gates_summary.append(run_gate("A03", "Preemptive SMP Scheduler", [sys.executable, "scripts/test_sched.py"]))

    # A04 & A05: Process lifecycle, IPC, and poll readiness
    gates_summary.append(run_gate("A04", "Process Lifecycle and ABI", [sys.executable, "scripts/test_process_ipc.py"]))
    gates_summary.append(run_gate("A05", "IPC Pipes and Readiness", [sys.executable, "scripts/test_process_ipc.py"]))

    # A06: Hardware drivers and block I/O
    gates_summary.append(run_gate("A06", "PCI and Virtio Drivers", [sys.executable, "scripts/test_pci_virtio.py"]))

    # A07: Filesystem storage and crash recovery
    gates_summary.append(run_gate("A07", "StrataFS and WAL Recovery", [sys.executable, "scripts/test_stratafs.py"]))

    # A08: Networking stack
    gates_summary.append(run_gate("A08", "Original Network Stack", [sys.executable, "scripts/test_network.py"]))

    # A09: Native userland and distributed ledger
    gates_summary.append(run_gate("A09", "Userland Ledger Cluster", [sys.executable, "scripts/test_ledger_cluster.py"]))

    # A10: Soak and stability
    gates_summary.append(run_gate("A10", "Depth and Guest Soak", [sys.executable, "scripts/test_soak.py"]))

    # A11: Quantitative benchmarks and fuzzing
    gates_summary.append(run_gate("A11-1", "Performance Benchmarks", [sys.executable, "scripts/run_benchmarks.py"]))
    gates_summary.append(run_gate("A11-2", "Differential Fuzzing Suite", [sys.executable, "scripts/run_fuzz.py"]))

    # A12: Reproducible builds
    repro_result = perform_reproducible_builds()
    a12_passed = (repro_result is not None and repro_result.get("reproducible", False))
    gates_summary.append({
        "gate": "A12",
        "name": "Reproducibility and Acceptance Evidence",
        "status": "PASSED" if a12_passed else "FAILED",
        "exit_code": 0 if a12_passed else 1,
        "elapsed_seconds": 0.0,
        "log_file": "evidence/evidence_manifest.json"
    })

    # Generate Manifest
    overall_passed = all(g["status"] == "PASSED" for g in gates_summary)
    manifest = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime()),
        "overall_status": "PASSED" if overall_passed else "FAILED",
        "gates": gates_summary,
        "reproducibility": repro_result
    }

    manifest_path = os.path.join(EVIDENCE_DIR, "evidence_manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    print("\n###############################################################")
    print(f"  Master Acceptance Suite Result: {'PASSED' if overall_passed else 'FAILED'}")
    print(f"  Evidence manifest written to: {manifest_path}")
    print("###############################################################")
    return 0 if overall_passed else 1


if __name__ == "__main__":
    sys.exit(main())
