# Stratum Structured Trace Format Specification

Version: 1.0.0
Status: Frozen

## 1. Overview
The Stratum kernel implements per-CPU lockless circular trace buffers that record structured system events with nanosecond-resolution monotonic timestamps, causal identifiers, and explicit loss tracking.

## 2. Trace Event Binary Layout (`trace_event_t`, 32 bytes)
```c
#define TRACE_MAGIC 0x54524143 /* 'TRAC' */

typedef struct {
    uint64_t timestamp_ns;  /* Monotonic system time in nanoseconds */
    uint32_t cpu_id;        /* Logical CPU ID (0 .. MAX_CPUS-1) */
    uint32_t task_id;       /* Combined (pid << 16) | (tid & 0xFFFF) */
    uint16_t event_type;    /* TRACE_EVENT_* */
    uint16_t seq_num;       /* Per-CPU monotonically increasing sequence */
    uint32_t loss_count;    /* Number of events dropped on this CPU */
    uint64_t arg0;          /* Event-specific payload 0 */
    uint64_t arg1;          /* Event-specific payload 1 */
} __attribute__((packed)) trace_event_t;
```

## 3. Core Event Types
| Code | Identifier | Description | `arg0` | `arg1` |
| --- | --- | --- | --- | --- |
| 0x0001 | `TRACE_SCHED_SWITCH` | Context switch | Previous task ID | Next task ID |
| 0x0002 | `TRACE_SCHED_WAKE` | Task unblocked | Target task ID | Wake reason |
| 0x0003 | `TRACE_SCHED_BLOCK` | Task blocked | Block reason | Wait queue address |
| 0x0010 | `TRACE_PAGE_FAULT` | Memory access fault | Fault address (CR2) | Error code |
| 0x0011 | `TRACE_VM_MAP` | Address space mapping | Virtual address | Page count |
| 0x0012 | `TRACE_VM_UNMAP` | Address space unmapping | Virtual address | Page count |
| 0x0013 | `TRACE_TLB_SHOOTDOWN` | Cross-CPU TLB flush | Initiator CPU | Target CPU mask |
| 0x0020 | `TRACE_SYSCALL_ENTER`| System call invoked | Syscall number | Argument 0 |
| 0x0021 | `TRACE_SYSCALL_EXIT` | System call returned | Syscall number | Return value |
| 0x0030 | `TRACE_BLOCK_SUBMIT` | Virtio block submit | Sector number | Sector count |
| 0x0031 | `TRACE_BLOCK_DONE` | Virtio block complete | Sector number | Status code |
| 0x0032 | `TRACE_FS_TXN_COMMIT`| Journal commit | Transaction ID | Block count |
| 0x0040 | `TRACE_NET_RX` | Packet received | Packet length | Protocol |
| 0x0041 | `TRACE_NET_TX` | Packet transmitted | Packet length | Protocol |
| 0x0042 | `TRACE_TCP_STATE` | TCP state change | Old state << 16 | New state |
| 0x0050 | `TRACE_PROC_FORK` | Process spawned | Parent PID | Child PID |
| 0x0051 | `TRACE_PROC_EXEC` | Process replaced | PID | Hash of path |
| 0x0052 | `TRACE_PROC_EXIT` | Process terminated | PID | Exit code |

## 4. Circular Buffer Organization and Loss Counters
- Each online CPU maintains a dedicated 64 KiB circular memory ring (2048 `trace_event_t` records).
- Producers write lock-free using `atomic_fetch_add` on the buffer sequence counter.
- When the write pointer overtakes the read pointer or ring capacity, an atomic drop counter is incremented and embedded in the subsequent trace event (`loss_count`).
- A user tool or debug extractor parses the buffers and outputs deterministic timelines.
