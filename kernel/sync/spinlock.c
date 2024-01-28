#include <kernel/spinlock.h>

void spin_lock_init(spinlock_t *lock) {
    if (!lock) return;
    __atomic_store_n(&lock->users, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&lock->ticket, 0, __ATOMIC_RELAXED);
}

void spin_lock(spinlock_t *lock) {
    if (!lock) return;
    uint32_t my_ticket = __atomic_fetch_add(&lock->users, 1, __ATOMIC_SEQ_CST);
    while (__atomic_load_n(&lock->ticket, __ATOMIC_ACQUIRE) != my_ticket) {
        cpu_pause();
    }
}

bool spin_trylock(spinlock_t *lock) {
    if (!lock) return false;
    uint32_t me = __atomic_load_n(&lock->users, __ATOMIC_RELAXED);
    uint32_t serving = __atomic_load_n(&lock->ticket, __ATOMIC_RELAXED);

    if (me != serving) {
        return false;
    }

    return __atomic_compare_exchange_n(&lock->users, &me, me + 1, false,
                                      __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
}

void spin_unlock(spinlock_t *lock) {
    if (!lock) return;
    __atomic_fetch_add(&lock->ticket, 1, __ATOMIC_RELEASE);
}
