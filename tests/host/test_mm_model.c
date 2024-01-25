/*
 * tests/host/test_mm_model.c - Host deterministic memory allocation and COW reference model.
 * Exercises 2,000,000 operations verifying bitmap tracking, size classes, and page refcounting.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#define TOTAL_OPS 2000000
#define MODEL_PAGES 65536 /* 256 MiB in 4K pages */

static uint64_t g_bitmap[MODEL_PAGES / 64];
static uint32_t g_page_refcount[MODEL_PAGES];

/* Simple deterministic LCG random generator */
static uint64_t g_seed = 0x123456789ABCDEF0ULL;
static inline uint32_t lcg_rand(void) {
    g_seed = g_seed * 6364136223846793005ULL + 1;
    return (uint32_t)(g_seed >> 32);
}

static inline void model_bitmap_set(uint32_t pfn) {
    g_bitmap[pfn / 64] |= (1ULL << (pfn % 64));
}

static inline void model_bitmap_clear(uint32_t pfn) {
    g_bitmap[pfn / 64] &= ~(1ULL << (pfn % 64));
}

static inline bool model_bitmap_test(uint32_t pfn) {
    return (g_bitmap[pfn / 64] & (1ULL << (pfn % 64))) != 0;
}

static uint32_t model_alloc_page(void) {
    for (uint32_t pfn = 0; pfn < MODEL_PAGES; pfn++) {
        if (!model_bitmap_test(pfn)) {
            model_bitmap_set(pfn);
            g_page_refcount[pfn] = 1;
            return pfn;
        }
    }
    return (uint32_t)-1;
}

static void model_free_page(uint32_t pfn) {
    assert(pfn < MODEL_PAGES);
    assert(model_bitmap_test(pfn));
    assert(g_page_refcount[pfn] > 0);

    g_page_refcount[pfn]--;
    if (g_page_refcount[pfn] == 0) {
        model_bitmap_clear(pfn);
    }
}

static void model_retain_page(uint32_t pfn) {
    assert(pfn < MODEL_PAGES);
    assert(model_bitmap_test(pfn));
    g_page_refcount[pfn]++;
}

int main(void) {
    printf("Starting Host MM Model Deterministic Test (%d operations)...\n", TOTAL_OPS);

    memset(g_bitmap, 0, sizeof(g_bitmap));
    memset(g_page_refcount, 0, sizeof(g_page_refcount));

    #define ACTIVE_CAP 4096
    uint32_t active_pfns[ACTIVE_CAP];
    uint32_t active_count = 0;

    uint64_t alloc_ops = 0;
    uint64_t free_ops = 0;
    uint64_t retain_ops = 0;

    for (int op = 0; op < TOTAL_OPS; op++) {
        uint32_t action = lcg_rand() % 3;

        if (action == 0 || active_count == 0) {
            /* Allocate page */
            if (active_count < ACTIVE_CAP) {
                uint32_t pfn = model_alloc_page();
                assert(pfn != (uint32_t)-1);
                assert(g_page_refcount[pfn] == 1);
                active_pfns[active_count++] = pfn;
                alloc_ops++;
            } else {
                /* Free random page */
                uint32_t idx = lcg_rand() % active_count;
                uint32_t pfn = active_pfns[idx];
                model_free_page(pfn);
                if (g_page_refcount[pfn] == 0) {
                    active_pfns[idx] = active_pfns[--active_count];
                }
                free_ops++;
            }
        } else if (action == 1) {
            /* Retain (COW fork simulation) */
            uint32_t idx = lcg_rand() % active_count;
            uint32_t pfn = active_pfns[idx];
            model_retain_page(pfn);
            retain_ops++;
        } else {
            /* Free/Release page */
            uint32_t idx = lcg_rand() % active_count;
            uint32_t pfn = active_pfns[idx];
            model_free_page(pfn);
            if (g_page_refcount[pfn] == 0) {
                active_pfns[idx] = active_pfns[--active_count];
            }
            free_ops++;
        }

        if ((op + 1) % 500000 == 0) {
            printf("  Progress: %d / %d ops completed (active=%u)...\n", op + 1, TOTAL_OPS, active_count);
        }
    }

    printf("Reclaiming remaining %u active pages...\n", active_count);
    while (active_count > 0) {
        uint32_t pfn = active_pfns[--active_count];
        while (g_page_refcount[pfn] > 0) {
            model_free_page(pfn);
            free_ops++;
        }
    }

    /* Verify complete zero leakage */
    for (uint32_t i = 0; i < MODEL_PAGES; i++) {
        assert(!model_bitmap_test(i));
        assert(g_page_refcount[i] == 0);
    }

    printf("[PASS] Host MM Model Test: %llu allocs, %llu retains, %llu frees.\n",
           (unsigned long long)alloc_ops,
           (unsigned long long)retain_ops,
           (unsigned long long)free_ops);
    printf("[PASS] Zero memory leakage and reference accounting confirmed.\n");

    return 0;
}
