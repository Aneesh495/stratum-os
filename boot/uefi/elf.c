#include "elf.h"

static void efi_memset(void *dest, int val, uint64_t count) {
    uint8_t *d = (uint8_t *)dest;
    uint8_t v = (uint8_t)val;
    while (count--) {
        *d++ = v;
    }
}

int elf_validate_header(const Elf64_Ehdr *ehdr, uint64_t file_size) {
    if (!ehdr) return -1;
    if (file_size < sizeof(Elf64_Ehdr)) return -2;

    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return -3; /* Invalid ELF magic */
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        return -4; /* Not 64-bit */
    }

    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return -5; /* Not little-endian */
    }

    if (ehdr->e_machine != EM_X86_64) {
        return -6; /* Not x86-64 */
    }

    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        return -7; /* Not executable */
    }

    if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * sizeof(Elf64_Phdr) > file_size) {
        return -8; /* Program headers out of file bounds */
    }

    return 0; /* Valid */
}

EFI_STATUS elf_load_segments(EFI_BOOT_SERVICES *bs, EFI_FILE_PROTOCOL *file, const Elf64_Ehdr *ehdr, elf_loaded_image_t *out_info) {
    uint64_t phdr_table_size = (uint64_t)ehdr->e_phnum * sizeof(Elf64_Phdr);
    Elf64_Phdr *phdrs = NULL;

    EFI_STATUS status = bs->AllocatePool(EfiLoaderData, phdr_table_size, (void **)&phdrs);
    if (status != EFI_SUCCESS) {
        return status;
    }

    /* Seek to program header table */
    status = file->SetPosition(file, ehdr->e_phoff);
    if (status != EFI_SUCCESS) {
        bs->FreePool(phdrs);
        return status;
    }

    uint64_t read_bytes = phdr_table_size;
    status = file->Read(file, &read_bytes, phdrs);
    if (status != EFI_SUCCESS || read_bytes != phdr_table_size) {
        bs->FreePool(phdrs);
        return EFI_LOAD_ERROR;
    }

    uint64_t min_vaddr = 0xFFFFFFFFFFFFFFFFULL;
    uint64_t max_vaddr = 0;

    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            if (phdrs[i].p_vaddr < min_vaddr) {
                min_vaddr = phdrs[i].p_vaddr;
            }
            uint64_t end = phdrs[i].p_vaddr + phdrs[i].p_memsz;
            if (end > max_vaddr) {
                max_vaddr = end;
            }
        }
    }

    if (min_vaddr >= max_vaddr) {
        bs->FreePool(phdrs);
        return EFI_LOAD_ERROR;
    }

    uint64_t total_span = max_vaddr - min_vaddr;
    uint64_t page_count = (total_span + 0xFFF) / 0x1000;

    uint64_t phys_base = 0;
    status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, page_count, &phys_base);
    if (status != EFI_SUCCESS) {
        bs->FreePool(phdrs);
        return status;
    }

    /* Zero out the entire allocated memory span */
    efi_memset((void *)phys_base, 0, page_count * 0x1000);

    /* Load each PT_LOAD segment */
    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) {
            continue;
        }

        uint64_t seg_offset = phdrs[i].p_vaddr - min_vaddr;
        void *dest = (void *)(phys_base + seg_offset);

        if (phdrs[i].p_filesz > 0) {
            status = file->SetPosition(file, phdrs[i].p_offset);
            if (status != EFI_SUCCESS) {
                bs->FreePages(phys_base, page_count);
                bs->FreePool(phdrs);
                return status;
            }

            uint64_t seg_read = phdrs[i].p_filesz;
            status = file->Read(file, &seg_read, dest);
            if (status != EFI_SUCCESS || seg_read != phdrs[i].p_filesz) {
                bs->FreePages(phys_base, page_count);
                bs->FreePool(phdrs);
                return EFI_LOAD_ERROR;
            }
        }

        /* If memsz > filesz, the rest is zeroed (already done by full span zeroing) */
    }

    bs->FreePool(phdrs);

    out_info->phys_base = phys_base;
    out_info->phys_size = page_count * 0x1000;
    out_info->virt_base = min_vaddr;
    out_info->entry_point = ehdr->e_entry;

    return EFI_SUCCESS;
}
