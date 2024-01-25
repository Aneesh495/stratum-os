#include <kernel/slab.h>
#include <kernel/pmm.h>
#include <kernel/kernel.h>
#include <kernel/string.h>

#define SLAB_MAGIC 0x534C4142 /* "SLAB" */
#define LARG_MAGIC 0x4C415247 /* "LARG" */

#define NUM_SIZE_CLASSES 8
static const size_t g_size_classes[NUM_SIZE_CLASSES] = { 16, 32, 64, 128, 256, 512, 1024, 2048 };

typedef struct slab_page {
    uint32_t magic;
    kmem_cache_t *cache;
    void *free_list;
    uint32_t inuse;
    uint32_t capacity;
    struct slab_page *next;
} slab_page_t;

struct kmem_cache {
    char name[32];
    size_t obj_size;
    size_t align;
    slab_page_t *slabs_partial;
    slab_page_t *slabs_full;
    uint64_t total_allocs;
    uint64_t total_frees;
};

static kmem_cache_t g_general_caches[NUM_SIZE_CLASSES];

typedef struct {
    uint32_t magic;
    uint32_t order;
    size_t size;
} large_alloc_header_t;

void slab_init(void) {
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        kmem_cache_t *c = &g_general_caches[i];
        snprintf(c->name, sizeof(c->name), "kmalloc-%zu", g_size_classes[i]);
        c->obj_size = g_size_classes[i];
        c->align = (g_size_classes[i] >= 16) ? 16 : 8;
        c->slabs_partial = NULL;
        c->slabs_full = NULL;
        c->total_allocs = 0;
        c->total_frees = 0;
    }
    kprintf("[SLAB] Object cache allocator initialized with %d size classes (16B - 2048B)\n", NUM_SIZE_CLASSES);
}

kmem_cache_t *kmem_cache_create(const char *name, size_t obj_size, size_t align) {
    /* Allocate cache descriptor from general cache */
    kmem_cache_t *c = (kmem_cache_t *)kmalloc(sizeof(kmem_cache_t));
    if (!c) return NULL;

    strncpy(c->name, name ? name : "custom", sizeof(c->name) - 1);
    c->name[sizeof(c->name) - 1] = '\0';
    c->obj_size = (obj_size + 7) & ~7;
    c->align = align ? align : 8;
    c->slabs_partial = NULL;
    c->slabs_full = NULL;
    c->total_allocs = 0;
    c->total_frees = 0;

    return c;
}

static slab_page_t *alloc_slab_page(kmem_cache_t *cache) {
    uint64_t paddr = pmm_alloc_page();
    if (!paddr) return NULL;

    slab_page_t *slab = (slab_page_t *)phys_to_virt(paddr);
    slab->magic = SLAB_MAGIC;
    slab->cache = cache;
    slab->inuse = 0;
    slab->next = NULL;

    /* Build linked free list in remaining page space */
    size_t offset = (sizeof(slab_page_t) + cache->align - 1) & ~(cache->align - 1);
    slab->free_list = (void *)((uint8_t *)slab + offset);

    uint32_t count = 0;
    uint8_t *curr = (uint8_t *)slab->free_list;
    while (offset + cache->obj_size <= PAGE_SIZE) {
        uint8_t *next = curr + cache->obj_size;
        if (offset + 2 * cache->obj_size <= PAGE_SIZE) {
            *(void **)curr = (void *)next;
        } else {
            *(void **)curr = NULL;
        }
        curr = next;
        offset += cache->obj_size;
        count++;
    }

    slab->capacity = count;
    return slab;
}

void *kmem_cache_alloc(kmem_cache_t *cache) {
    if (!cache) return NULL;

    slab_page_t *slab = cache->slabs_partial;
    if (!slab) {
        slab = alloc_slab_page(cache);
        if (!slab) return NULL;

        slab->next = cache->slabs_partial;
        cache->slabs_partial = slab;
    }

    void *obj = slab->free_list;
    slab->free_list = *(void **)obj;
    slab->inuse++;
    cache->total_allocs++;

    /* If slab is now full, move to full list */
    if (slab->inuse == slab->capacity) {
        cache->slabs_partial = slab->next;
        slab->next = cache->slabs_full;
        cache->slabs_full = slab;
    }

    return obj;
}

void kmem_cache_free(kmem_cache_t *cache, void *obj) {
    if (!cache || !obj) return;

    /* Find slab header at page base */
    uint64_t page_base = ((uint64_t)obj) & ~(PAGE_SIZE - 1);
    slab_page_t *slab = (slab_page_t *)page_base;

    if (slab->magic != SLAB_MAGIC || slab->cache != cache) {
        kprintf("[SLAB] ERROR: Corrupted slab header on free %p\n", obj);
        return;
    }

    /* Return object to slab free list */
    *(void **)obj = slab->free_list;
    slab->free_list = obj;
    slab->inuse--;
    cache->total_frees++;

    /* If slab was full, move back to partial list */
    if (slab->inuse == slab->capacity - 1) {
        /* Remove from full list */
        slab_page_t **curr = &cache->slabs_full;
        while (*curr && *curr != slab) {
            curr = &(*curr)->next;
        }
        if (*curr) {
            *curr = slab->next;
        }
        slab->next = cache->slabs_partial;
        cache->slabs_partial = slab;
    } else if (slab->inuse == 0) {
        /* Completely empty slab: reclaim to PMM if there are other partials */
        if (cache->slabs_partial && cache->slabs_partial->next) {
            slab_page_t **curr = &cache->slabs_partial;
            while (*curr && *curr != slab) {
                curr = &(*curr)->next;
            }
            if (*curr) {
                *curr = slab->next;
            }
            uint64_t paddr = virt_to_phys(slab);
            pmm_free_page(paddr);
        }
    }
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Small allocation from size classes */
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= g_size_classes[i]) {
            return kmem_cache_alloc(&g_general_caches[i]);
        }
    }

    /* Large allocation directly from PMM */
    size_t total_size = size + sizeof(large_alloc_header_t);
    uint32_t pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;

    uint32_t order = 0;
    while ((1ULL << order) < pages) {
        order++;
    }

    uint64_t paddr = pmm_alloc_pages(order);
    if (!paddr) return NULL;

    large_alloc_header_t *hdr = (large_alloc_header_t *)phys_to_virt(paddr);
    hdr->magic = LARG_MAGIC;
    hdr->order = order;
    hdr->size = size;

    return (void *)(hdr + 1);
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;

    /* Check if object is from a small slab */
    uint64_t page_base = ((uint64_t)ptr) & ~(PAGE_SIZE - 1);
    slab_page_t *slab = (slab_page_t *)page_base;

    if (slab->magic == SLAB_MAGIC && slab->cache != NULL) {
        kmem_cache_free(slab->cache, ptr);
        return;
    }

    /* Check if large allocation */
    large_alloc_header_t *hdr = ((large_alloc_header_t *)ptr) - 1;
    if (hdr->magic == LARG_MAGIC) {
        uint32_t order = hdr->order;
        hdr->magic = 0; /* Clear magic to prevent double free */
        uint64_t paddr = virt_to_phys(hdr);
        pmm_free_pages(paddr, order);
        return;
    }

    kprintf("[SLAB] ERROR: kfree received unmanaged pointer %p\n", ptr);
}
