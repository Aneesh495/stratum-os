#ifndef STRATUM_KERNEL_SPINLOCK_H
#define STRATUM_KERNEL_SPINLOCK_H

#include <kernel/types.h>
#include <kernel/x86_64.h>

typedef struct {
    volatile uint32_t users;   /* Ticket counter for incoming acquirers */
    volatile uint32_t ticket;  /* Currently serving ticket */
} spinlock_t;

#define SPINLOCK_INIT ((spinlock_t){0, 0})

void spin_lock_init(spinlock_t *lock);
void spin_lock(spinlock_t *lock);
bool spin_trylock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);

static inline void spin_lock_irqsave(spinlock_t *lock, uint64_t *flags) {
    *flags = local_irq_save();
    spin_lock(lock);
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags) {
    spin_unlock(lock);
    local_irq_restore(flags);
}

#endif /* STRATUM_KERNEL_SPINLOCK_H */
