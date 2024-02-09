#include <kernel/user_elf.h>
#include <kernel/elf64.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/kernel.h>
#include <kernel/string.h>

int user_elf_load(const void *elf_data, size_t elf_size, user_program_t *out_prog) {
    if (!elf_data || elf_size < sizeof(Elf64_Ehdr) || !out_prog) {
        return -1;
    }

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)elf_data;

    /* Validate ELF magic */
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        kprintf("[USER_ELF] ERROR: Invalid ELF magic!\n");
        return -2;
    }

    /* Validate 64-bit x86-64 executable */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr->e_machine != EM_X86_64) {
        kprintf("[USER_ELF] ERROR: Unsupported ELF architecture!\n");
        return -3;
    }

    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        kprintf("[USER_ELF] ERROR: Not an executable ELF image!\n");
        return -4;
    }

    uint64_t ph_table_end = ehdr->e_phoff + (uint64_t)ehdr->e_phnum * sizeof(Elf64_Phdr);
    if (ph_table_end > elf_size) {
        kprintf("[USER_ELF] ERROR: Program header table out of bounds!\n");
        return -5;
    }

    /* 1. Allocate isolated user virtual address space */
    pml4_t *pml4 = vmm_create_address_space();
    if (!pml4) {
        kprintf("[USER_ELF] ERROR: Failed to allocate address space!\n");
        return -6;
    }

    const Elf64_Phdr *phdrs = (const Elf64_Phdr *)((const uint8_t *)elf_data + ehdr->e_phoff);

    /* 2. Load PT_LOAD segments */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;

        if (ph->p_memsz == 0) continue;

        if (ph->p_vaddr + ph->p_memsz > 0x0000800000000000ULL) {
            kprintf("[USER_ELF] ERROR: Segment vaddr 0x%lx exceeds user space!\n", ph->p_vaddr);
            vmm_destroy_address_space(pml4);
            return -7;
        }

        if (ph->p_offset + ph->p_filesz > elf_size) {
            kprintf("[USER_ELF] ERROR: Segment file offset exceeds image size!\n");
            vmm_destroy_address_space(pml4);
            return -8;
        }

        uint64_t seg_start = ph->p_vaddr;
        uint64_t seg_end   = ph->p_vaddr + ph->p_memsz;
        uint64_t page_start = seg_start & ~0xFFFULL;
        uint64_t page_end   = (seg_end + 0xFFFULL) & ~0xFFFULL;

        uint64_t flags = PTE_PRESENT | PTE_USER;
        if (ph->p_flags & PF_W) flags |= PTE_WRITABLE;
        if (!(ph->p_flags & PF_X)) flags |= PTE_NX;

        for (uint64_t va = page_start; va < page_end; va += PAGE_SIZE) {
            uint64_t paddr = 0, cur_flags = 0;
            if (vmm_get_mapping(pml4, va, &paddr, &cur_flags) != 0) {
                paddr = pmm_alloc_page();
                if (!paddr) {
                    kprintf("[USER_ELF] ERROR: Out of physical memory for user segment!\n");
                    vmm_destroy_address_space(pml4);
                    return -9;
                }
                memset(phys_to_virt(paddr), 0, PAGE_SIZE);
                vmm_map_page(pml4, va, paddr, flags);
            }

            /* Copy file slice into page */
            uint64_t page_va_start = va;
            uint64_t page_va_end   = va + PAGE_SIZE;

            uint64_t copy_va_start = (seg_start > page_va_start) ? seg_start : page_va_start;
            uint64_t copy_va_end   = ((ph->p_vaddr + ph->p_filesz) < page_va_end) ?
                                     (ph->p_vaddr + ph->p_filesz) : page_va_end;

            if (copy_va_end > copy_va_start) {
                uint64_t file_offset = ph->p_offset + (copy_va_start - seg_start);
                uint64_t page_offset = copy_va_start - page_va_start;
                size_t   copy_len    = copy_va_end - copy_va_start;

                memcpy((uint8_t *)phys_to_virt(paddr) + page_offset,
                       (const uint8_t *)elf_data + file_offset,
                       copy_len);
            }
        }
    }

    /* 3. Allocate and map user stack */
    uint64_t stack_base = USER_STACK_TOP - (USER_STACK_PAGES * PAGE_SIZE);
    for (uint64_t va = stack_base; va < USER_STACK_TOP; va += PAGE_SIZE) {
        uint64_t stack_paddr = pmm_alloc_page();
        if (!stack_paddr) {
            kprintf("[USER_ELF] ERROR: Out of physical memory for user stack!\n");
            vmm_destroy_address_space(pml4);
            return -10;
        }
        memset(phys_to_virt(stack_paddr), 0, PAGE_SIZE);
        vmm_map_page(pml4, va, stack_paddr, PTE_PRESENT | PTE_USER | PTE_WRITABLE | PTE_NX);
    }

    out_prog->entry_point    = ehdr->e_entry;
    out_prog->user_stack_top = USER_STACK_TOP - 16;
    out_prog->address_space  = pml4;

    kprintf("[USER_ELF] ELF64 loaded successfully (entry=0x%016lx, stack=0x%016lx)\n",
            out_prog->entry_point, out_prog->user_stack_top);
    return 0;
}
