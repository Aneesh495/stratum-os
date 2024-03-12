#include <kernel/panic.h>
#include <kernel/kernel.h>
#include <kernel/x86_64.h>
#include <kernel/vmm.h>
#include <kernel/string.h>
#include <stdarg.h>

#include <kernel/pmm.h>
#include <kernel/smp.h>
#include <kernel/sched.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/vfs.h>
#include <kernel/tcp.h>
#include <kernel/ledger.h>

extern void _start(void);
extern void kmain(uint64_t magic, const void *handoff);
extern void syscall_entry(void);

typedef struct {
    uint64_t    addr;
    const char *name;
} kernel_symbol_t;

/* Symbol table for primary kernel routines */
static const kernel_symbol_t g_kernel_symbols[] = {
    { (uint64_t)_start, "_start" },
    { (uint64_t)kmain, "kmain" },
    { (uint64_t)gdt_init, "gdt_init" },
    { (uint64_t)idt_init, "idt_init" },
    { (uint64_t)pmm_init, "pmm_init" },
    { (uint64_t)vmm_init, "vmm_init" },
    { (uint64_t)smp_init, "smp_init" },
    { (uint64_t)sched_init, "sched_init" },
    { (uint64_t)syscall_entry, "syscall_entry" },
    { (uint64_t)vfs_open, "vfs_open" },
    { (uint64_t)tcp_input, "tcp_input" },
    { (uint64_t)ledger_init, "ledger_init" },
};

#define NUM_KERNEL_SYMBOLS (sizeof(g_kernel_symbols) / sizeof(g_kernel_symbols[0]))

const char *panic_lookup_symbol(uint64_t addr, uint64_t *out_offset) {
    const kernel_symbol_t *best = NULL;
    for (size_t i = 0; i < NUM_KERNEL_SYMBOLS; i++) {
        if (addr >= g_kernel_symbols[i].addr) {
            if (!best || g_kernel_symbols[i].addr > best->addr) {
                best = &g_kernel_symbols[i];
            }
        }
    }

    if (best && (addr - best->addr) < 0x10000) {
        if (out_offset) *out_offset = addr - best->addr;
        return best->name;
    }

    if (out_offset) *out_offset = 0;
    return "unknown";
}

void panic_dump_registers(uint64_t rip, uint64_t rsp, uint64_t rbp, uint64_t cr2, uint64_t cr3) {
    kprintf("\n[PANIC-DIAGNOSTIC] CPU Register Dump:\n");
    kprintf("  RIP: 0x%016lx  RSP: 0x%016lx\n", rip, rsp);
    kprintf("  RBP: 0x%016lx  CR2: 0x%016lx\n", rbp, cr2);
    kprintf("  CR3: 0x%016lx  RFL: 0x%016lx\n", cr3, read_rflags());

    uint64_t sym_offset = 0;
    const char *sym = panic_lookup_symbol(rip, &sym_offset);
    kprintf("  Faulting Symbol: <%s+0x%lx>\n", sym, sym_offset);
}

void panic_stack_trace(uint64_t rbp, size_t max_depth) {
    kprintf("[PANIC-DIAGNOSTIC] Call Stack Backtrace:\n");
    uint64_t curr_rbp = rbp;

    for (size_t depth = 0; depth < max_depth; depth++) {
        if (curr_rbp < 0xFFFF800000000000ULL || (curr_rbp & 7) != 0) {
            break;
        }

        uint64_t *frame = (uint64_t *)curr_rbp;
        uint64_t next_rbp = frame[0];
        uint64_t ret_rip = frame[1];

        if (ret_rip == 0) break;

        uint64_t offset = 0;
        const char *sym = panic_lookup_symbol(ret_rip, &offset);
        kprintf("  [%02zu] RIP: 0x%016lx <%s+0x%lx> (RBP: 0x%016lx)\n",
                depth, ret_rip, sym, offset, curr_rbp);

        if (next_rbp <= curr_rbp) break; /* Avoid loops */
        curr_rbp = next_rbp;
    }
}

void panic_enhanced(const char *file, int line, const char *fmt, ...) {
    cli();

    kputs("\n=======================================================\n");
    kprintf("KERNEL PANIC at %s:%d\n", file ? file : "unknown", line);

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    kputs("Reason: ");
    kputs(buf);
    kputs("\n");

    uint64_t rbp, rsp;
    __asm__ volatile("movq %%rbp, %0" : "=r"(rbp));
    __asm__ volatile("movq %%rsp, %0" : "=r"(rsp));
    panic_dump_registers(0, rsp, rbp, read_cr2(), read_cr3());
    panic_stack_trace(rbp, 8);

    kputs("=======================================================\n");
    kputs("System halted.\n");

    while (1) {
        hlt();
    }
}
