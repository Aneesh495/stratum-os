#ifndef STRATUM_KERNEL_PANIC_H
#define STRATUM_KERNEL_PANIC_H

#include <kernel/types.h>

void panic_dump_registers(uint64_t rip, uint64_t rsp, uint64_t rbp, uint64_t cr2, uint64_t cr3);
void panic_stack_trace(uint64_t rbp, size_t max_depth);
const char *panic_lookup_symbol(uint64_t addr, uint64_t *out_offset);
void panic_enhanced(const char *file, int line, const char *fmt, ...) __attribute__((noreturn));

#endif /* STRATUM_KERNEL_PANIC_H */
