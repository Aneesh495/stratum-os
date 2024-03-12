#include <kernel/process.h>
#include <kernel/slab.h>
#include <kernel/kernel.h>
#include <kernel/string.h>
#include <kernel/smp.h>
#include <kernel/vmm.h>
#include <shared/errno.h>

static process_t g_process_pool[PROCESS_MAX_COUNT];
static spinlock_t g_process_lock = SPINLOCK_INIT;
static uint32_t   g_next_pid = 1;

void process_init(void) {
    memset(g_process_pool, 0, sizeof(g_process_pool));
    for (int i = 0; i < PROCESS_MAX_COUNT; i++) {
        spin_lock_init(&g_process_pool[i].lock);
        wait_queue_init(&g_process_pool[i].wait_child);
    }
}

process_t *process_get_current(void) {
    uint32_t cpu_id = smp_get_cpu_id();
    thread_t *curr = g_runqueues[cpu_id].current_thread;
    return curr ? curr->process : NULL;
}

process_t *process_find(uint32_t pid) {
    uint64_t flags;
    spin_lock_irqsave(&g_process_lock, &flags);
    for (int i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (g_process_pool[i].state != PROC_STATE_UNUSED && g_process_pool[i].pid == pid) {
            spin_unlock_irqrestore(&g_process_lock, flags);
            return &g_process_pool[i];
        }
    }
    spin_unlock_irqrestore(&g_process_lock, flags);
    return NULL;
}

static void user_process_starter(void *arg) {
    user_program_t *prog = (user_program_t *)arg;
    user_enter(prog->entry_point, prog->user_stack_top);
}

process_t *process_create_init(user_program_t *prog) {
    if (!prog) return NULL;

    uint64_t flags;
    spin_lock_irqsave(&g_process_lock, &flags);

    process_t *proc = &g_process_pool[0];
    memset(proc, 0, sizeof(process_t));
    proc->pid = 1;
    g_next_pid = 2;
    proc->ppid = 0;
    proc->state = PROC_STATE_ALIVE;
    strncpy(proc->name, "init", sizeof(proc->name) - 1);
    proc->address_space = prog->address_space;
    spin_lock_init(&proc->lock);
    wait_queue_init(&proc->wait_child);

    fd_table_init(&proc->fds);
    file_t *c0 = file_create_console();
    file_t *c1 = file_create_console();
    file_t *c2 = file_create_console();
    fd_install(&proc->fds, 0, c0);
    fd_install(&proc->fds, 1, c1);
    fd_install(&proc->fds, 2, c2);

    spin_unlock_irqrestore(&g_process_lock, flags);

    thread_t *t = thread_create_user("init", user_process_starter, prog,
                                      THREAD_PRIO_NORMAL, prog->address_space);
    if (!t) return NULL;
    t->process = proc;
    proc->main_thread = t;

    return proc;
}

static void fork_child_entry(void *arg) {
    syscall_regs_t *kregs = (syscall_regs_t *)arg;
    kregs->rax = 0; /* Child process returns 0 from fork */
    user_enter_regs(kregs);
}

int32_t process_fork(syscall_regs_t *regs) {
    if (!regs) return -STRATUM_EINVAL;

    process_t *parent = process_get_current();
    if (!parent) return -STRATUM_ESRCH;

    uint64_t flags;
    spin_lock_irqsave(&g_process_lock, &flags);

    process_t *child = NULL;
    for (int i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (g_process_pool[i].state == PROC_STATE_UNUSED) {
            child = &g_process_pool[i];
            break;
        }
    }

    if (!child) {
        spin_unlock_irqrestore(&g_process_lock, flags);
        return -STRATUM_EAGAIN;
    }

    memset(child, 0, sizeof(process_t));
    child->pid = g_next_pid++;
    child->ppid = parent->pid;
    child->state = PROC_STATE_ALIVE;
    child->parent = parent;
    snprintf(child->name, sizeof(child->name), "proc_%u", child->pid);
    spin_lock_init(&child->lock);
    wait_queue_init(&child->wait_child);

    /* Clone virtual address space with Copy-On-Write */
    child->address_space = vmm_clone_address_space(parent->address_space);
    if (!child->address_space) {
        child->state = PROC_STATE_UNUSED;
        spin_unlock_irqrestore(&g_process_lock, flags);
        return -STRATUM_ENOMEM;
    }

    /* Duplicate file descriptors */
    fd_table_copy(&child->fds, &parent->fds);

    /* Link child into parent's hierarchy */
    child->next_sibling = parent->children;
    parent->children = child;

    spin_unlock_irqrestore(&g_process_lock, flags);

    /* Allocate copy of register frame for child thread */
    syscall_regs_t *child_regs = (syscall_regs_t *)kmalloc(sizeof(syscall_regs_t));
    if (!child_regs) {
        return -STRATUM_ENOMEM;
    }
    memcpy(child_regs, regs, sizeof(syscall_regs_t));
    child_regs->rax = 0;

    /* Create child execution thread */
    thread_t *ct = thread_create_user(child->name, fork_child_entry, child_regs,
                                      THREAD_PRIO_NORMAL, child->address_space);
    if (!ct) {
        kfree(child_regs);
        return -STRATUM_ENOMEM;
    }
    ct->process = child;
    child->main_thread = ct;

    return (int32_t)child->pid;
}

void process_exit(int status) {
    process_t *proc = process_get_current();
    if (!proc) {
        thread_exit();
        while (1) {
            __asm__ volatile("hlt");
        }
    }

    uint64_t flags;
    spin_lock_irqsave(&g_process_lock, &flags);

    proc->exit_code = status;
    proc->state = PROC_STATE_ZOMBIE;

    /* Destroy file descriptors */
    fd_table_destroy(&proc->fds);

    /* Reparent orphan children to PID 1 */
    process_t *init = &g_process_pool[0];
    if (proc != init && proc->children) {
        process_t *c = proc->children;
        while (c) {
            c->parent = init;
            process_t *next = c->next_sibling;
            c->next_sibling = init->children;
            init->children = c;
            c = next;
        }
        proc->children = NULL;
    }

    /* Wake parent */
    if (proc->parent) {
        wait_queue_wake_all(&proc->parent->wait_child);
    }

    /* If PID 1 exited, signal test completion */
    if (proc->pid == 1) {
        g_user_exit_code = status;
        g_user_init_finished = true;
        smp_send_ipi(g_cpus[0].lapic_id, VEC_IPI_RESCHED);
    }

    spin_unlock_irqrestore(&g_process_lock, flags);

    thread_exit();
    while (1) {
        __asm__ volatile("hlt");
    }
}

int32_t process_waitpid(int32_t pid, int *status, int options) {
    process_t *parent = process_get_current();
    if (!parent) return -STRATUM_ESRCH;

    while (1) {
        uint64_t flags;
        spin_lock_irqsave(&g_process_lock, &flags);

        bool has_matching_child = false;
        process_t *prev = NULL;
        process_t *cur = parent->children;

        while (cur) {
            if (pid == -1 || pid == (int32_t)cur->pid) {
                has_matching_child = true;

                if (cur->state == PROC_STATE_ZOMBIE) {
                    /* Child reaped */
                    int exit_val = cur->exit_code;
                    uint32_t reaped_pid = cur->pid;

                    /* Unlink from parent */
                    if (prev) {
                        prev->next_sibling = cur->next_sibling;
                    } else {
                        parent->children = cur->next_sibling;
                    }

                    cur->state = PROC_STATE_DEAD;
                    if (cur->address_space) {
                        vmm_destroy_address_space(cur->address_space);
                        cur->address_space = NULL;
                    }
                    cur->state = PROC_STATE_UNUSED;

                    spin_unlock_irqrestore(&g_process_lock, flags);

                    if (status) {
                        copy_to_user(status, &exit_val, sizeof(int));
                    }
                    return (int32_t)reaped_pid;
                }
            }
            prev = cur;
            cur = cur->next_sibling;
        }

        if (!has_matching_child) {
            spin_unlock_irqrestore(&g_process_lock, flags);
            return -STRATUM_ECHILD;
        }

        if (options & WNOHANG) {
            spin_unlock_irqrestore(&g_process_lock, flags);
            return 0;
        }

        spin_unlock_irqrestore(&g_process_lock, flags);

        /* Wait for child state change */
        wait_queue_wait(&parent->wait_child);
    }
}
