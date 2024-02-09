#include <kernel/syscall.h>
#include <kernel/kernel.h>
#include <kernel/smp.h>
#include <kernel/sched.h>
#include <kernel/vmm.h>
#include <kernel/x86_64.h>
#include <shared/syscall_nums.h>
#include <shared/errno.h>

extern void syscall_entry(void);

static inline bool is_user_range_valid(const void *addr, size_t len) {
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + len;
    if (end < start) return false; /* Overflow */
    if (end > USER_SPACE_TOP) return false; /* Above canonical lower half */
    return true;
}

void syscall_init_cpu(void) {
    /* 1. Enable System Call Extensions (SCE) in EFER */
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1);

    /* 2. Configure STAR MSR (0xC0000081):
     * Bits 47:32 = KERNEL_CS (0x08) -> Syscall sets CS=0x08, SS=0x10
     * Bits 63:48 = USER_CS_BASE (0x10) -> Sysret sets SS=0x1B, CS=0x23
     */
    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(MSR_STAR, star);

    /* 3. Configure LSTAR MSR (0xC0000082): Target RIP for syscall */
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* 4. Configure SFMASK MSR (0xC0000084):
     * Mask out IF (0x200), DF (0x400), TF (0x100), NT (0x4000), AC (0x40000)
     */
    wrmsr(MSR_SFMASK, 0x44700);

    /* 5. Set KERNEL_GS_BASE to current cpu_t pointer so swapgs works on syscall */
    cpu_t *cpu = smp_get_current_cpu();
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)cpu);
}

void syscall_init(void) {
    syscall_init_cpu();
    kprintf("[SYSCALL] Fast syscall/sysret MSRs configured (Ring 3 user ABI active)\n");
}

static int64_t sys_write_handler(int fd, const void *u_buf, size_t count) {
    if (!is_user_range_valid(u_buf, count)) {
        return -STRATUM_EFAULT;
    }

    if (fd != 1 && fd != 2) {
        return -STRATUM_EBADF;
    }

    char kbuf[256];
    size_t remaining = count;
    const char *src = (const char *)u_buf;

    while (remaining > 0) {
        size_t chunk = (remaining > sizeof(kbuf) - 1) ? (sizeof(kbuf) - 1) : remaining;
        if (copy_from_user(kbuf, src, chunk) != 0) {
            return -STRATUM_EFAULT;
        }
        kbuf[chunk] = '\0';
        kputs(kbuf);
        src += chunk;
        remaining -= chunk;
    }

    return (int64_t)count;
}

volatile bool g_user_init_finished = false;
volatile int  g_user_exit_code = 0;

int64_t syscall_dispatch(syscall_regs_t *regs) {
    if (!regs) return -STRATUM_EINVAL;

    uint64_t nr = regs->rax;

    switch (nr) {
    case SYS_exit: {
        int status = (int)regs->rdi;
        kprintf("[USER] Process exited with code %d\n", status);
        g_user_exit_code = status;
        g_user_init_finished = true;
        thread_exit();
        return 0;
    }

    case SYS_write: {
        return sys_write_handler((int)regs->rdi, (const void *)regs->rsi, (size_t)regs->rdx);
    }

    case SYS_read: {
        /* Early stdin stub */
        return 0;
    }

    case SYS_getpid: {
        uint32_t cpu_id = smp_get_cpu_id();
        runqueue_t *rq = &g_runqueues[cpu_id];
        return rq->current_thread ? (int64_t)rq->current_thread->tid : 1;
    }

    case SYS_nanosleep: {
        uint32_t ms = (uint32_t)regs->rdi;
        thread_sleep_ms(ms);
        return 0;
    }

    case SYS_clock_gettime: {
        return (int64_t)timer_get_uptime_ms();
    }

    case SYS_thread_create:
    case SYS_thread_exit:
    case SYS_thread_self: {
        thread_yield();
        return 0;
    }

    default:
        kprintf("[SYSCALL] Unhandled syscall #%lu from RIP=0x%016lx\n", nr, regs->rip);
        return -STRATUM_ENOSYS;
    }
}
