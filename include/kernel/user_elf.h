#ifndef STRATUM_KERNEL_USER_ELF_H
#define STRATUM_KERNEL_USER_ELF_H

#include <kernel/types.h>
#include <kernel/vmm.h>

#define USER_STACK_TOP  0x00007FFFFFFFF000ULL
#define USER_STACK_PAGES 16 /* 64 KiB user stack */

typedef struct {
    uint64_t entry_point;
    uint64_t user_stack_top;
    pml4_t  *address_space;
} user_program_t;

int user_elf_load(const void *elf_data, size_t elf_size, user_program_t *out_prog);

#endif /* STRATUM_KERNEL_USER_ELF_H */
