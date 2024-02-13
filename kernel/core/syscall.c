#include <kernel/syscall.h>
#include <kernel/kernel.h>
#include <kernel/smp.h>
#include <kernel/sched.h>
#include <kernel/vmm.h>
#include <kernel/x86_64.h>
#include <kernel/process.h>
#include <kernel/file.h>
#include <kernel/pipe.h>
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

volatile bool g_user_init_finished = false;
volatile int  g_user_exit_code = 0;

static int64_t sys_read_impl(int fd, void *u_buf, size_t count) {
    if (!is_user_range_valid(u_buf, count)) {
        return -STRATUM_EFAULT;
    }

    process_t *proc = process_get_current();
    if (!proc) return -STRATUM_ESRCH;

    file_t *f = fd_get(&proc->fds, fd);
    if (!f) return -STRATUM_EBADF;

    if (!f->ops || !f->ops->read) {
        file_close(f);
        return -STRATUM_EINVAL;
    }

    char kbuf[512];
    size_t to_read = (count > sizeof(kbuf)) ? sizeof(kbuf) : count;
    int64_t res = f->ops->read(f, kbuf, to_read);

    if (res > 0) {
        if (copy_to_user(u_buf, kbuf, (size_t)res) != 0) {
            file_close(f);
            return -STRATUM_EFAULT;
        }
    }

    file_close(f);
    return res;
}

static int64_t sys_write_impl(int fd, const void *u_buf, size_t count) {
    if (!is_user_range_valid(u_buf, count)) {
        return -STRATUM_EFAULT;
    }

    process_t *proc = process_get_current();
    if (!proc) return -STRATUM_ESRCH;

    file_t *f = fd_get(&proc->fds, fd);
    if (!f) return -STRATUM_EBADF;

    if (!f->ops || !f->ops->write) {
        file_close(f);
        return -STRATUM_EINVAL;
    }

    char kbuf[512];
    size_t written = 0;
    const char *src = (const char *)u_buf;

    while (written < count) {
        size_t chunk = (count - written > sizeof(kbuf)) ? sizeof(kbuf) : (count - written);
        if (copy_from_user(kbuf, src + written, chunk) != 0) {
            file_close(f);
            return -STRATUM_EFAULT;
        }

        int64_t res = f->ops->write(f, kbuf, chunk);
        if (res < 0) {
            file_close(f);
            return (written > 0) ? (int64_t)written : res;
        }
        written += (size_t)res;
        if ((size_t)res < chunk) break;
    }

    file_close(f);
    return (int64_t)written;
}

static int64_t sys_pipe_impl(int *u_pipefd) {
    if (!is_user_range_valid(u_pipefd, 2 * sizeof(int))) {
        return -STRATUM_EFAULT;
    }

    process_t *proc = process_get_current();
    if (!proc) return -STRATUM_ESRCH;

    file_t *rf = NULL, *wf = NULL;
    int res = pipe_create(&rf, &wf);
    if (res != 0) return res;

    int rfd = fd_alloc(&proc->fds, rf);
    if (rfd < 0) {
        file_close(rf);
        file_close(wf);
        return rfd;
    }

    int wfd = fd_alloc(&proc->fds, wf);
    if (wfd < 0) {
        fd_close(&proc->fds, rfd);
        file_close(wf);
        return wfd;
    }

    int k_fds[2] = { rfd, wfd };
    if (copy_to_user(u_pipefd, k_fds, sizeof(k_fds)) != 0) {
        fd_close(&proc->fds, rfd);
        fd_close(&proc->fds, wfd);
        return -STRATUM_EFAULT;
    }

    return 0;
}

static int64_t sys_poll_impl(struct pollfd *u_fds, uint32_t nfds, int timeout_ms) {
    if (nfds > 32) return -STRATUM_EINVAL;
    if (!is_user_range_valid(u_fds, nfds * sizeof(struct pollfd))) {
        return -STRATUM_EFAULT;
    }

    process_t *proc = process_get_current();
    if (!proc) return -STRATUM_ESRCH;

    struct pollfd k_fds[32];
    if (copy_from_user(k_fds, u_fds, nfds * sizeof(struct pollfd)) != 0) {
        return -STRATUM_EFAULT;
    }

    uint64_t start_ms = timer_get_uptime_ms();

    while (1) {
        int ready_count = 0;

        for (uint32_t i = 0; i < nfds; i++) {
            k_fds[i].revents = 0;
            if (k_fds[i].fd < 0) continue;

            file_t *f = fd_get(&proc->fds, k_fds[i].fd);
            if (!f) {
                k_fds[i].revents = POLLNVAL;
                ready_count++;
                continue;
            }

            if (f->ops && f->ops->poll) {
                int rev = f->ops->poll(f, (uint32_t)k_fds[i].events);
                k_fds[i].revents = (int16_t)rev;
                if (rev != 0) {
                    ready_count++;
                }
            }
            file_close(f);
        }

        if (ready_count > 0) {
            copy_to_user(u_fds, k_fds, nfds * sizeof(struct pollfd));
            return ready_count;
        }

        if (timeout_ms == 0) {
            copy_to_user(u_fds, k_fds, nfds * sizeof(struct pollfd));
            return 0;
        }

        if (timeout_ms > 0) {
            uint64_t elapsed = timer_get_uptime_ms() - start_ms;
            if (elapsed >= (uint64_t)timeout_ms) {
                copy_to_user(u_fds, k_fds, nfds * sizeof(struct pollfd));
                return 0;
            }
        }

        thread_sleep_ms(2);
    }
}

int64_t syscall_dispatch(syscall_regs_t *regs) {
    if (!regs) return -STRATUM_EINVAL;

    uint64_t nr = regs->rax;

    switch (nr) {
    case SYS_exit: {
        int status = (int)regs->rdi;
        process_exit(status);
        return 0;
    }

    case SYS_fork: {
        return process_fork(regs);
    }

    case SYS_waitpid: {
        int32_t pid = (int32_t)regs->rdi;
        int *status = (int *)regs->rsi;
        int options = (int)regs->rdx;
        return process_waitpid(pid, status, options);
    }

    case SYS_getpid: {
        process_t *p = process_get_current();
        return p ? (int64_t)p->pid : 1;
    }

    case SYS_read: {
        return sys_read_impl((int)regs->rdi, (void *)regs->rsi, (size_t)regs->rdx);
    }

    case SYS_write: {
        return sys_write_impl((int)regs->rdi, (const void *)regs->rsi, (size_t)regs->rdx);
    }

    case SYS_close: {
        process_t *p = process_get_current();
        if (!p) return -STRATUM_ESRCH;
        return fd_close(&p->fds, (int)regs->rdi);
    }

    case SYS_dup2: {
        process_t *p = process_get_current();
        if (!p) return -STRATUM_ESRCH;
        return fd_dup2(&p->fds, (int)regs->rdi, (int)regs->rsi);
    }

    case SYS_pipe: {
        return sys_pipe_impl((int *)regs->rdi);
    }

    case SYS_poll: {
        return sys_poll_impl((struct pollfd *)regs->rdi, (uint32_t)regs->rsi, (int)regs->rdx);
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
