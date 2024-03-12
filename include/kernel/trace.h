#ifndef STRATUM_KERNEL_TRACE_H
#define STRATUM_KERNEL_TRACE_H

#include <kernel/types.h>

#define TRACE_RING_BUFFER_ENTRIES 1024
#define TRACE_MAX_CPUS           16

/* Standard Trace Event IDs */
#define TRACE_EVENT_NONE         0
#define TRACE_EVENT_SCHED_SWITCH 1
#define TRACE_EVENT_SYSCALL      2
#define TRACE_EVENT_PAGE_FAULT   3
#define TRACE_EVENT_BLOCK_IO     4
#define TRACE_EVENT_NET_PACKET   5
#define TRACE_EVENT_LEDGER_TX    6
#define TRACE_EVENT_LEDGER_BLOCK 7
#define TRACE_EVENT_USER_CUSTOM  100

typedef struct {
    uint64_t timestamp_tsc;
    uint32_t cpu_id;
    uint32_t event_id;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
} __attribute__((packed)) trace_entry_t;

typedef struct {
    volatile uint64_t head;
    volatile uint64_t tail;
    volatile uint64_t lost_events;
    trace_entry_t     entries[TRACE_RING_BUFFER_ENTRIES];
} trace_per_cpu_t;

void trace_init(void);
void trace_emit(uint32_t event_id, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4);
uint64_t trace_get_losses(uint32_t cpu_id);
void trace_dump(void);

#endif /* STRATUM_KERNEL_TRACE_H */
