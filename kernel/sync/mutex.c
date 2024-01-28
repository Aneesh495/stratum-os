#include <kernel/mutex.h>
#include <kernel/x86_64.h>

void mutex_init(mutex_t *m) {
    if (!m) return;
    spin_lock_init(&m->lock);
    m->state = 0;
    m->owner = NULL;
    m->wait_list = NULL;
}

void mutex_lock(mutex_t *m) {
    if (!m) return;
    while (1) {
        spin_lock(&m->lock);
        if (m->state == 0) {
            m->state = 1;
            spin_unlock(&m->lock);
            return;
        }
        spin_unlock(&m->lock);
        cpu_pause();
    }
}

bool mutex_trylock(mutex_t *m) {
    if (!m) return false;
    if (!spin_trylock(&m->lock)) {
        return false;
    }
    if (m->state == 0) {
        m->state = 1;
        spin_unlock(&m->lock);
        return true;
    }
    spin_unlock(&m->lock);
    return false;
}

void mutex_unlock(mutex_t *m) {
    if (!m) return;
    spin_lock(&m->lock);
    m->state = 0;
    m->owner = NULL;
    spin_unlock(&m->lock);
}
