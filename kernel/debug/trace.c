#include <kernel/trace.h>
#include <kernel/smp.h>
#include <kernel/kernel.h>
#include <kernel/string.h>

static trace_per_cpu_t g_trace_cpus[TRACE_MAX_CPUS];
static bool g_trace_inited = false;

static inline uint64_t rdtsc_pure(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void trace_init(void) {
    memset(g_trace_cpus, 0, sizeof(g_trace_cpus));
    g_trace_inited = true;
    kprintf("[TRACE] Per-CPU lockless tracing ring buffers initialized (capacity: %d entries/CPU).\n",
            TRACE_RING_BUFFER_ENTRIES);
}

void trace_emit(uint32_t event_id, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    if (!g_trace_inited) return;

    uint32_t cpu_id = smp_get_cpu_id();
    if (cpu_id >= TRACE_MAX_CPUS) cpu_id = 0;

    trace_per_cpu_t *tb = &g_trace_cpus[cpu_id];
    uint64_t current_head = tb->head;
    uint64_t next_head = current_head + 1;

    /* Check if full without blocking: if next would overtake tail by capacity */
    if ((next_head - tb->tail) >= TRACE_RING_BUFFER_ENTRIES) {
        __atomic_add_fetch(&tb->lost_events, 1, __ATOMIC_RELAXED);
        /* Advance tail to drop oldest entry */
        __atomic_add_fetch(&tb->tail, 1, __ATOMIC_RELAXED);
    }

    uint64_t idx = current_head % TRACE_RING_BUFFER_ENTRIES;
    trace_entry_t *entry = &tb->entries[idx];
    entry->timestamp_tsc = rdtsc_pure();
    entry->cpu_id = cpu_id;
    entry->event_id = event_id;
    entry->arg1 = a1;
    entry->arg2 = a2;
    entry->arg3 = a3;
    entry->arg4 = a4;

    __atomic_store_n(&tb->head, next_head, __ATOMIC_RELEASE);
}

uint64_t trace_get_losses(uint32_t cpu_id) {
    if (cpu_id >= TRACE_MAX_CPUS) return 0;
    return g_trace_cpus[cpu_id].lost_events;
}

void trace_dump(void) {
    kprintf("[TRACE] Dump of active CPU trace ring buffers:\n");
    for (uint32_t c = 0; c < TRACE_MAX_CPUS; c++) {
        trace_per_cpu_t *tb = &g_trace_cpus[c];
        if (tb->head > 0 || tb->lost_events > 0) {
            kprintf("  CPU %u: Head=%lu Tail=%lu Lost=%lu\n",
                    c, tb->head, tb->tail, tb->lost_events);
        }
    }
}
