#ifndef STRATUM_KERNEL_PMM_H
#define STRATUM_KERNEL_PMM_H

#include <kernel/types.h>
#include <shared/boot_info.h>

#define PAGE_SIZE       4096
#define PAGE_SHIFT      12
#define HHDM_BASE       0xFFFF800000000000ULL

extern uint64_t g_kernel_phys_base;

/* Direct physical to virtual translation macros */
static inline void *phys_to_virt(uint64_t paddr) {
    return (void *)(HHDM_BASE + paddr);
}

static inline uint64_t virt_to_phys(const void *vaddr) {
    uint64_t addr = (uint64_t)vaddr;
    if (addr >= 0xFFFFFFFF80000000ULL) {
        return (addr - 0xFFFFFFFF80000000ULL) + g_kernel_phys_base;
    }
    if (addr >= HHDM_BASE) {
        return addr - HHDM_BASE;
    }
    return addr;
}

#define PAGE_FLAG_FREE      0x01
#define PAGE_FLAG_RESERVED  0x02
#define PAGE_FLAG_SLAB      0x04
#define PAGE_FLAG_PAGETABLE 0x08
#define PAGE_FLAG_USER      0x10
#define PAGE_FLAG_COW       0x20

typedef struct page {
    uint32_t flags;
    uint32_t refcount;
    uint32_t order;
    struct page *next;
} page_t;

void     pmm_init(const boot_handoff_t *handoff);
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_pages(uint32_t order);
void     pmm_free_page(uint64_t paddr);
void     pmm_free_pages(uint64_t paddr, uint32_t order);

void     pmm_page_retain(uint64_t paddr);
void     pmm_page_release(uint64_t paddr);
uint32_t pmm_page_refcount(uint64_t paddr);

uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_used_pages(void);

#endif /* STRATUM_KERNEL_PMM_H */
