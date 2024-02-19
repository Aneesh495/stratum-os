#include <kernel/virtqueue.h>
#include <kernel/pmm.h>
#include <kernel/slab.h>
#include <kernel/kernel.h>
#include <kernel/string.h>
#include <shared/errno.h>

#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))

virtqueue_t *virtqueue_create(uint16_t queue_idx, uint16_t queue_size,
                              virtqueue_notify_fn notify, void *notify_ctx) {
    if (queue_size == 0 || (queue_size & (queue_size - 1)) != 0) {
        return NULL; /* Queue size must be a power of two */
    }

    virtqueue_t *vq = (virtqueue_t *)kmalloc(sizeof(virtqueue_t));
    if (!vq) return NULL;

    memset(vq, 0, sizeof(virtqueue_t));
    spin_lock_init(&vq->lock);
    vq->queue_idx = queue_idx;
    vq->queue_size = queue_size;
    vq->num_free = queue_size;
    vq->free_head = 0;
    vq->last_used_idx = 0;
    vq->notify = notify;
    vq->notify_ctx = notify_ctx;

    /* Calculate layout sizes */
    size_t desc_size = queue_size * sizeof(vring_desc_t);
    size_t avail_size = sizeof(vring_avail_t) + queue_size * sizeof(uint16_t) + sizeof(uint16_t);
    size_t avail_offset = desc_size;
    size_t used_offset = ALIGN_UP(avail_offset + avail_size, 4096);
    size_t used_size = sizeof(vring_used_t) + queue_size * sizeof(vring_used_elem_t) + sizeof(uint16_t);
    size_t total_size = used_offset + used_size;

    size_t pages_needed = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t order = 0;
    while ((1ULL << order) < pages_needed) {
        order++;
    }

    uint64_t mem_phys = pmm_alloc_pages(order);
    if (!mem_phys) {
        kfree(vq);
        return NULL;
    }

    void *mem_virt = phys_to_virt(mem_phys);
    memset(mem_virt, 0, (1ULL << order) * PAGE_SIZE);

    vq->mem_phys = mem_phys;
    vq->mem_size = (1ULL << order) * PAGE_SIZE;
    vq->mem_virt = mem_virt;

    vq->desc = (vring_desc_t *)mem_virt;
    vq->avail = (vring_avail_t *)((uint8_t *)mem_virt + avail_offset);
    vq->used = (vring_used_t *)((uint8_t *)mem_virt + used_offset);

    /* Allocate cookies array */
    vq->cookies = (void **)kmalloc(queue_size * sizeof(void *));
    if (!vq->cookies) {
        pmm_free_pages(mem_phys, order);
        kfree(vq);
        return NULL;
    }
    memset(vq->cookies, 0, queue_size * sizeof(void *));

    /* Initialize descriptor free list */
    for (uint16_t i = 0; i < queue_size - 1; i++) {
        vq->desc[i].next = i + 1;
    }
    vq->desc[queue_size - 1].next = 0;

    return vq;
}

void virtqueue_destroy(virtqueue_t *vq) {
    if (!vq) return;

    if (vq->mem_phys) {
        uint32_t order = 0;
        size_t pages = vq->mem_size / PAGE_SIZE;
        while ((1ULL << order) < pages) {
            order++;
        }
        pmm_free_pages(vq->mem_phys, order);
    }

    if (vq->cookies) {
        kfree(vq->cookies);
    }

    kfree(vq);
}

int virtqueue_add_buf(virtqueue_t *vq, const vring_buf_t *bufs,
                      uint32_t num_out, uint32_t num_in, void *cookie) {
    if (!vq || !bufs) return -STRATUM_EINVAL;

    uint32_t total = num_out + num_in;
    if (total == 0 || total > vq->queue_size) return -STRATUM_EINVAL;

    uint64_t flags;
    spin_lock_irqsave(&vq->lock, &flags);

    if (vq->num_free < total) {
        spin_unlock_irqrestore(&vq->lock, flags);
        return -STRATUM_ENOMEM;
    }

    uint16_t head = vq->free_head;
    uint16_t curr = head;

    for (uint32_t i = 0; i < total; i++) {
        vq->desc[curr].addr = bufs[i].phys_addr;
        vq->desc[curr].len = bufs[i].len;
        vq->desc[curr].flags = 0;

        if (bufs[i].is_write) {
            vq->desc[curr].flags |= VRING_DESC_F_WRITE;
        }

        if (i < total - 1) {
            vq->desc[curr].flags |= VRING_DESC_F_NEXT;
            curr = vq->desc[curr].next;
        }
    }

    /* Update free list pointer and count */
    vq->free_head = vq->desc[curr].next;
    vq->num_free -= (uint16_t)total;

    /* Store cookie on head descriptor */
    vq->cookies[head] = cookie;

    /* Add head to available ring */
    uint16_t avail_idx = vq->avail->idx;
    vq->avail->ring[avail_idx % vq->queue_size] = head;

    /* Memory barrier before updating index */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    vq->avail->idx = avail_idx + 1;

    spin_unlock_irqrestore(&vq->lock, flags);
    return 0;
}

void virtqueue_kick(virtqueue_t *vq) {
    if (!vq) return;

    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    /* If device did not suppress notifications (VRING_AVAIL_F_NO_INTERRUPT / used flags) */
    if (!(vq->used->flags & VRING_USED_F_NO_NOTIFY)) {
        if (vq->notify) {
            vq->notify(vq->notify_ctx, vq->queue_idx);
        }
    }
}

void *virtqueue_get_buf(virtqueue_t *vq, uint32_t *out_len) {
    if (!vq) return NULL;

    uint64_t flags;
    spin_lock_irqsave(&vq->lock, &flags);

    if (vq->last_used_idx == vq->used->idx) {
        spin_unlock_irqrestore(&vq->lock, flags);
        return NULL;
    }

    /* Memory barrier to ensure completed descriptor read order */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    uint16_t used_slot = vq->last_used_idx % vq->queue_size;
    vring_used_elem_t *elem = &vq->used->ring[used_slot];
    uint16_t head = (uint16_t)elem->id;

    if (out_len) {
        *out_len = elem->len;
    }

    void *cookie = vq->cookies[head];
    vq->cookies[head] = NULL;

    /* Reclaim descriptor chain back to free list */
    uint16_t curr = head;
    uint16_t count = 0;
    while (1) {
        count++;
        if (!(vq->desc[curr].flags & VRING_DESC_F_NEXT)) {
            break;
        }
        curr = vq->desc[curr].next;
    }

    /* Link freed chain to current free head */
    vq->desc[curr].next = vq->free_head;
    vq->free_head = head;
    vq->num_free += count;

    /* Advance monotonic last_used_idx (natural 16-bit wrap) */
    vq->last_used_idx++;

    spin_unlock_irqrestore(&vq->lock, flags);
    return cookie;
}

bool virtqueue_has_used(virtqueue_t *vq) {
    if (!vq) return false;
    return (vq->last_used_idx != vq->used->idx);
}
