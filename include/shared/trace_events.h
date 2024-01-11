#ifndef STRATUM_SHARED_TRACE_EVENTS_H
#define STRATUM_SHARED_TRACE_EVENTS_H

#if defined(__has_include)
  #if __has_include(<kernel/types.h>)
    #include <kernel/types.h>
  #else
    #include <stdint.h>
  #endif
#else
  #include <stdint.h>
#endif

#define TRACE_MAGIC 0x54524143 /* 'TRAC' */

#define TRACE_SCHED_SWITCH      0x0001
#define TRACE_SCHED_WAKE        0x0002
#define TRACE_SCHED_BLOCK       0x0003
#define TRACE_PAGE_FAULT        0x0010
#define TRACE_VM_MAP            0x0011
#define TRACE_VM_UNMAP          0x0012
#define TRACE_TLB_SHOOTDOWN     0x0013
#define TRACE_SYSCALL_ENTER     0x0020
#define TRACE_SYSCALL_EXIT      0x0021
#define TRACE_BLOCK_SUBMIT      0x0030
#define TRACE_BLOCK_DONE        0x0031
#define TRACE_FS_TXN_COMMIT     0x0032
#define TRACE_NET_RX            0x0040
#define TRACE_NET_TX            0x0041
#define TRACE_TCP_STATE         0x0042
#define TRACE_PROC_FORK         0x0050
#define TRACE_PROC_EXEC         0x0051
#define TRACE_PROC_EXIT         0x0052

typedef struct {
    uint64_t timestamp_ns;  /* Monotonic time in nanoseconds */
    uint32_t cpu_id;        /* Logical CPU ID */
    uint32_t task_id;       /* (pid << 16) | (tid & 0xFFFF) */
    uint16_t event_type;    /* TRACE_* */
    uint16_t seq_num;       /* Per-CPU sequence */
    uint32_t loss_count;    /* Drop counter */
    uint64_t arg0;          /* Custom payload 0 */
    uint64_t arg1;          /* Custom payload 1 */
} __attribute__((packed)) trace_event_t;

#endif /* STRATUM_SHARED_TRACE_EVENTS_H */
