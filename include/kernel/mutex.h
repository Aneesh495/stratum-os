#ifndef STRATUM_KERNEL_MUTEX_H
#define STRATUM_KERNEL_MUTEX_H

#include <kernel/types.h>
#include <kernel/spinlock.h>

typedef struct mutex {
    spinlock_t  lock;
    volatile uint32_t state;     /* 0 = free, 1 = held */
    void       *owner;           /* Owning thread pointer or CPU ID */
    void       *wait_list;       /* Linked list of waiting threads */
} mutex_t;

#define MUTEX_INIT ((mutex_t){SPINLOCK_INIT, 0, NULL, NULL})

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
bool mutex_trylock(mutex_t *m);
void mutex_unlock(mutex_t *m);

#endif /* STRATUM_KERNEL_MUTEX_H */
