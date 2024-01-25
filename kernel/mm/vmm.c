#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/kernel.h>
#include <kernel/string.h>
#include <kernel/x86_64.h>

pml4_t *g_kernel_pml4 = NULL;

extern char _kernel_start[];
extern char _text_start[];
extern char _text_end[];
extern char _rodata_start[];
extern char _rodata_end[];
extern char _data_start[];
extern char _data_end[];
extern char _bss_start[];
extern char _bss_end[];
extern char _kernel_end[];

static page_table_t *get_or_create_table(uint64_t *entry, uint64_t flags) {
    if (*entry & PTE_PRESENT) {
        uint64_t paddr = *entry & ~0xFFFULL;
        return (page_table_t *)phys_to_virt(paddr);
    }

    uint64_t paddr = pmm_alloc_page();
    if (!paddr) return NULL;

    *entry = paddr | flags | PTE_PRESENT | PTE_WRITABLE;
    return (page_table_t *)phys_to_virt(paddr);
}

int vmm_map_page(pml4_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    if (!pml4 || (vaddr & 0xFFF) != 0 || (paddr & 0xFFF) != 0) {
        return -1;
    }

    uint64_t pml4_idx = PML4_INDEX(vaddr);
    uint64_t pdpt_idx = PDPT_INDEX(vaddr);
    uint64_t pd_idx   = PD_INDEX(vaddr);
    uint64_t pt_idx   = PT_INDEX(vaddr);

    uint64_t intermediate_flags = (flags & PTE_USER) ? (PTE_PRESENT | PTE_WRITABLE | PTE_USER) : (PTE_PRESENT | PTE_WRITABLE);

    page_table_t *pdpt = get_or_create_table(&pml4->entries[pml4_idx], intermediate_flags);
    if (!pdpt) return -2;

    page_table_t *pd = get_or_create_table(&pdpt->entries[pdpt_idx], intermediate_flags);
    if (!pd) return -3;

    page_table_t *pt = get_or_create_table(&pd->entries[pd_idx], intermediate_flags);
    if (!pt) return -4;

    pt->entries[pt_idx] = paddr | flags | PTE_PRESENT;
    invlpg(vaddr);

    return 0;
}

int vmm_unmap_page(pml4_t *pml4, uint64_t vaddr) {
    if (!pml4 || (vaddr & 0xFFF) != 0) return -1;

    uint64_t pml4_idx = PML4_INDEX(vaddr);
    uint64_t pdpt_idx = PDPT_INDEX(vaddr);
    uint64_t pd_idx   = PD_INDEX(vaddr);
    uint64_t pt_idx   = PT_INDEX(vaddr);

    if (!(pml4->entries[pml4_idx] & PTE_PRESENT)) return 0;
    page_table_t *pdpt = (page_table_t *)phys_to_virt(pml4->entries[pml4_idx] & ~0xFFFULL);

    if (!(pdpt->entries[pdpt_idx] & PTE_PRESENT)) return 0;
    page_table_t *pd = (page_table_t *)phys_to_virt(pdpt->entries[pdpt_idx] & ~0xFFFULL);

    if (!(pd->entries[pd_idx] & PTE_PRESENT)) return 0;
    page_table_t *pt = (page_table_t *)phys_to_virt(pd->entries[pd_idx] & ~0xFFFULL);

    pt->entries[pt_idx] = 0;
    invlpg(vaddr);

    return 0;
}

int vmm_get_mapping(pml4_t *pml4, uint64_t vaddr, uint64_t *out_paddr, uint64_t *out_flags) {
    if (!pml4) return -1;

    uint64_t pml4_idx = PML4_INDEX(vaddr);
    uint64_t pdpt_idx = PDPT_INDEX(vaddr);
    uint64_t pd_idx   = PD_INDEX(vaddr);
    uint64_t pt_idx   = PT_INDEX(vaddr);

    if (!(pml4->entries[pml4_idx] & PTE_PRESENT)) return -2;
    page_table_t *pdpt = (page_table_t *)phys_to_virt(pml4->entries[pml4_idx] & ~0xFFFULL);

    if (!(pdpt->entries[pdpt_idx] & PTE_PRESENT)) return -3;
    page_table_t *pd = (page_table_t *)phys_to_virt(pdpt->entries[pdpt_idx] & ~0xFFFULL);

    if (!(pd->entries[pd_idx] & PTE_PRESENT)) return -4;
    page_table_t *pt = (page_table_t *)phys_to_virt(pd->entries[pd_idx] & ~0xFFFULL);

    if (!(pt->entries[pt_idx] & PTE_PRESENT)) return -5;

    if (out_paddr) *out_paddr = pt->entries[pt_idx] & ~0xFFFULL;
    if (out_flags) *out_flags = pt->entries[pt_idx] & 0xFFF;

    return 0;
}

int vmm_protect(pml4_t *pml4, uint64_t vaddr, uint64_t flags) {
    uint64_t paddr = 0, old_flags = 0;
    int res = vmm_get_mapping(pml4, vaddr, &paddr, &old_flags);
    if (res != 0) return res;

    return vmm_map_page(pml4, vaddr, paddr, flags);
}

pml4_t *vmm_create_address_space(void) {
    uint64_t paddr = pmm_alloc_page();
    if (!paddr) return NULL;

    pml4_t *pml4 = (pml4_t *)phys_to_virt(paddr);
    memset(pml4, 0, PAGE_SIZE);

    /* Share upper-half kernel mappings (entries 256..511) */
    for (int i = 256; i < 512; i++) {
        pml4->entries[i] = g_kernel_pml4->entries[i];
    }

    return pml4;
}

pml4_t *vmm_clone_address_space(pml4_t *src) {
    if (!src) return NULL;

    pml4_t *dst = vmm_create_address_space();
    if (!dst) return NULL;

    /* Copy user space mappings (0..255) with Copy-On-Write (COW) */
    for (int pml4_i = 0; pml4_i < 256; pml4_i++) {
        if (!(src->entries[pml4_i] & PTE_PRESENT)) continue;

        page_table_t *src_pdpt = (page_table_t *)phys_to_virt(src->entries[pml4_i] & ~0xFFFULL);
        for (int pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            if (!(src_pdpt->entries[pdpt_i] & PTE_PRESENT)) continue;

            page_table_t *src_pd = (page_table_t *)phys_to_virt(src_pdpt->entries[pdpt_i] & ~0xFFFULL);
            for (int pd_i = 0; pd_i < 512; pd_i++) {
                if (!(src_pd->entries[pd_i] & PTE_PRESENT)) continue;

                page_table_t *src_pt = (page_table_t *)phys_to_virt(src_pd->entries[pd_i] & ~0xFFFULL);
                for (int pt_i = 0; pt_i < 512; pt_i++) {
                    uint64_t pte = src_pt->entries[pt_i];
                    if (!(pte & PTE_PRESENT)) continue;

                    uint64_t paddr = pte & ~0xFFFULL;
                    uint64_t flags = pte & 0xFFF;

                    /* If page is writable, convert to COW (read-only + PTE_COW) */
                    if (flags & PTE_WRITABLE) {
                        flags &= ~PTE_WRITABLE;
                        flags |= PTE_COW;
                        src_pt->entries[pt_i] = paddr | flags;

                        /* Invalidate parent page */
                        uint64_t vaddr = ((uint64_t)pml4_i << 39) |
                                         ((uint64_t)pdpt_i << 30) |
                                         ((uint64_t)pd_i << 21)   |
                                         ((uint64_t)pt_i << 12);
                        invlpg(vaddr);
                    }

                    /* Retain physical page refcount */
                    pmm_page_retain(paddr);

                    /* Map into child */
                    uint64_t vaddr = ((uint64_t)pml4_i << 39) |
                                     ((uint64_t)pdpt_i << 30) |
                                     ((uint64_t)pd_i << 21)   |
                                     ((uint64_t)pt_i << 12);
                    vmm_map_page(dst, vaddr, paddr, flags);
                }
            }
        }
    }

    return dst;
}

void vmm_destroy_address_space(pml4_t *pml4) {
    if (!pml4 || pml4 == g_kernel_pml4) return;

    /* Free user-space page tables and release physical pages (0..255) */
    for (int pml4_i = 0; pml4_i < 256; pml4_i++) {
        if (!(pml4->entries[pml4_i] & PTE_PRESENT)) continue;

        page_table_t *pdpt = (page_table_t *)phys_to_virt(pml4->entries[pml4_i] & ~0xFFFULL);
        for (int pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            if (!(pdpt->entries[pdpt_i] & PTE_PRESENT)) continue;

            page_table_t *pd = (page_table_t *)phys_to_virt(pdpt->entries[pdpt_i] & ~0xFFFULL);
            for (int pd_i = 0; pd_i < 512; pd_i++) {
                if (!(pd->entries[pd_i] & PTE_PRESENT)) continue;

                page_table_t *pt = (page_table_t *)phys_to_virt(pd->entries[pd_i] & ~0xFFFULL);
                for (int pt_i = 0; pt_i < 512; pt_i++) {
                    uint64_t pte = pt->entries[pt_i];
                    if (pte & PTE_PRESENT) {
                        uint64_t paddr = pte & ~0xFFFULL;
                        pmm_page_release(paddr);
                    }
                }
                pmm_free_page(virt_to_phys(pt));
            }
            pmm_free_page(virt_to_phys(pd));
        }
        pmm_free_page(virt_to_phys(pdpt));
    }

    pmm_free_page(virt_to_phys(pml4));
}

void vmm_switch_address_space(pml4_t *pml4) {
    if (!pml4) return;
    uint64_t cr3 = virt_to_phys(pml4);
    write_cr3(cr3);
}

void vmm_page_fault_handler(interrupt_frame_t *frame) {
    uint64_t fault_addr = read_cr2();
    uint64_t error_code = frame->error_code;

    bool is_present = (error_code & 1) != 0;
    bool is_write   = (error_code & 2) != 0;
    bool is_user    = (error_code & 4) != 0;

    /* Check if this is a Copy-On-Write (COW) write fault */
    if (is_present && is_write) {
        uint64_t paddr = 0, flags = 0;
        uint64_t cr3 = read_cr3();
        pml4_t *curr_pml4 = (pml4_t *)phys_to_virt(cr3);

        if (vmm_get_mapping(curr_pml4, fault_addr & ~0xFFFULL, &paddr, &flags) == 0) {
            if (flags & PTE_COW) {
                uint32_t ref = pmm_page_refcount(paddr);
                if (ref == 1) {
                    /* Sole owner: make writable directly */
                    flags &= ~PTE_COW;
                    flags |= PTE_WRITABLE;
                    vmm_map_page(curr_pml4, fault_addr & ~0xFFFULL, paddr, flags);
                    return;
                } else {
                    /* Shared page: allocate new physical page, copy data, remap */
                    uint64_t new_paddr = pmm_alloc_page();
                    if (!new_paddr) {
                        panic("COW fault handler failed to allocate physical page!");
                    }

                    memcpy(phys_to_virt(new_paddr), phys_to_virt(paddr), PAGE_SIZE);

                    flags &= ~PTE_COW;
                    flags |= PTE_WRITABLE;
                    vmm_map_page(curr_pml4, fault_addr & ~0xFFFULL, new_paddr, flags);

                    pmm_page_release(paddr);
                    return;
                }
            }
        }
    }

    /* Check for demand paging (unmapped page access in valid user range) */
    if (!is_present && is_user && fault_addr >= 0x400000 && fault_addr < 0x7FFFFFFFF000ULL) {
        uint64_t cr3 = read_cr3();
        pml4_t *curr_pml4 = (pml4_t *)phys_to_virt(cr3);
        uint64_t new_page = pmm_alloc_page();
        if (new_page) {
            vmm_map_page(curr_pml4, fault_addr & ~0xFFFULL, new_page, PTE_USER | PTE_WRITABLE);
            return;
        }
    }

    /* Unhandled or illegal page fault */
    kprintf("\n[PAGE FAULT] %s at 0x%016lx from %s (RIP=0x%016lx)\n",
            is_write ? "Write fault" : "Read fault",
            fault_addr,
            is_user ? "User space (Ring 3)" : "Kernel space (Ring 0)",
            frame->rip);

    exception_dispatch(frame);
}

void vmm_init(const boot_handoff_t *handoff) {
    /* 1. Allocate permanent kernel PML4 */
    uint64_t pml4_phys = pmm_alloc_page();
    g_kernel_pml4 = (pml4_t *)phys_to_virt(pml4_phys);
    memset(g_kernel_pml4, 0, PAGE_SIZE);

    /* 2. Map Direct Physical Map (HHDM: 0xFFFF800000000000 to first 4 GiB) */
    /* Map 4 GiB using 2 MiB large pages for maximum TLB performance */
    uint64_t hhdm_pdpt_phys = pmm_alloc_page();
    page_table_t *hhdm_pdpt = (page_table_t *)phys_to_virt(hhdm_pdpt_phys);
    memset(hhdm_pdpt, 0, PAGE_SIZE);

    g_kernel_pml4->entries[PML4_INDEX(HHDM_BASE)] = hhdm_pdpt_phys | PTE_PRESENT | PTE_WRITABLE;

    for (uint64_t g = 0; g < 4; g++) {
        uint64_t pd_phys = pmm_alloc_page();
        page_table_t *pd = (page_table_t *)phys_to_virt(pd_phys);
        memset(pd, 0, PAGE_SIZE);

        hhdm_pdpt->entries[g] = pd_phys | PTE_PRESENT | PTE_WRITABLE;

        for (uint64_t m = 0; m < 512; m++) {
            uint64_t paddr = (g * 512 + m) * 0x200000ULL;
            pd->entries[m] = paddr | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE;
        }
    }

    /* 3. Map Kernel Image Segments at 0xFFFFFFFF80000000 */
    uint64_t kphys = handoff->kernel_phys_base;
    uint64_t kvirt_base = 0xFFFFFFFF80000000ULL;

    /* Map text: RX */
    uint64_t text_start_va = (uint64_t)_text_start;
    uint64_t text_end_va = (uint64_t)_text_end;
    for (uint64_t va = text_start_va; va < text_end_va; va += PAGE_SIZE) {
        uint64_t pa = kphys + (va - kvirt_base);
        vmm_map_page(g_kernel_pml4, va, pa, PTE_PRESENT);
    }

    /* Map rodata: R, NX */
    uint64_t rodata_start_va = (uint64_t)_rodata_start;
    uint64_t rodata_end_va = (uint64_t)_rodata_end;
    for (uint64_t va = rodata_start_va; va < rodata_end_va; va += PAGE_SIZE) {
        uint64_t pa = kphys + (va - kvirt_base);
        vmm_map_page(g_kernel_pml4, va, pa, PTE_PRESENT | PTE_NX);
    }

    /* Map data & bss: RW, NX */
    uint64_t data_start_va = (uint64_t)_data_start;
    uint64_t bss_end_va = (uint64_t)_bss_end;
    for (uint64_t va = data_start_va; va < bss_end_va; va += PAGE_SIZE) {
        uint64_t pa = kphys + (va - kvirt_base);
        vmm_map_page(g_kernel_pml4, va, pa, PTE_PRESENT | PTE_WRITABLE | PTE_NX);
    }

    /* 4. Map Device MMIO / Framebuffer if available */
    if (handoff->fb_base_phys != 0) {
        uint64_t fb_pages = (handoff->fb_height * handoff->fb_stride * 4 + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t i = 0; i < fb_pages; i++) {
            uint64_t pa = handoff->fb_base_phys + i * PAGE_SIZE;
            vmm_map_page(g_kernel_pml4, pa, pa, PTE_PRESENT | PTE_WRITABLE | PTE_PCD);
        }
    }

    /* 5. Switch CR3 to the permanent kernel PML4 */
    write_cr3(pml4_phys);

    /* 6. Register Page Fault exception handler (vector 14) */
    register_interrupt_handler(14, vmm_page_fault_handler);

    kprintf("[VMM] Virtual memory manager initialized with 4-level paging (PML4=0x%lx)\n", pml4_phys);
    kprintf("[VMM]   HHDM Base: 0x%016llx mapped (4 GiB)\n", HHDM_BASE);
    kprintf("[VMM]   Kernel text [RX]:   0x%lx - 0x%lx\n", text_start_va, text_end_va);
    kprintf("[VMM]   Kernel rodata [R]:  0x%lx - 0x%lx\n", rodata_start_va, rodata_end_va);
    kprintf("[VMM]   Kernel data [RW]:   0x%lx - 0x%lx\n", data_start_va, bss_end_va);
    kprintf("[VMM]   Page fault handler and COW tracking enabled\n");
}
