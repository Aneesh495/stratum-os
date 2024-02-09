#ifndef STRATUM_KERNEL_SYSCALL_H
#define STRATUM_KERNEL_SYSCALL_H

#include <kernel/types.h>
#include <shared/syscall_nums.h>
#include <shared/errno.h>

#define USER_SPACE_TOP 0x0000800000000000ULL

typedef struct {
    /* General Purpose Registers pushed in syscall_entry */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r9;    /* Arg 6 */
    uint64_t r8;    /* Arg 5 */
    uint64_t r10;   /* Arg 4 */
    uint64_t rdx;   /* Arg 3 */
    uint64_t rsi;   /* Arg 2 */
    uint64_t rdi;   /* Arg 1 */
    uint64_t rax;   /* Syscall number / return value */

    /* User state saved on syscall entry */
    uint64_t rip;   /* User RIP */
    uint64_t cs;    /* User CS */
    uint64_t rflags;/* User RFLAGS */
    uint64_t rsp;   /* User RSP */
    uint64_t ss;    /* User SS */
} __attribute__((packed)) syscall_regs_t;

void    syscall_init(void);
void    syscall_init_cpu(void);
int64_t syscall_dispatch(syscall_regs_t *regs);

/* Safe usercopy memory primitives with fault recovery */
int copy_to_user(void *dst_u, const void *src_k, size_t n);
int copy_from_user(void *dst_k, const void *src_u, size_t n);

/* Transition to Ring 3 */
void user_enter(uint64_t entry_rip, uint64_t user_rsp) __attribute__((noreturn));

extern volatile bool g_user_init_finished;
extern volatile int  g_user_exit_code;

#endif /* STRATUM_KERNEL_SYSCALL_H */
