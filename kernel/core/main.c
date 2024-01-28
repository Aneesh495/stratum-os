#include <kernel/kernel.h>
#include <kernel/boot.h>
#include <kernel/uart.h>
#include <kernel/fb.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>
#include <kernel/slab.h>
#include <kernel/vmm.h>
#include <kernel/smp.h>
#include <kernel/spinlock.h>
#include <kernel/mutex.h>
#include <kernel/x86_64.h>

void kmain(boot_handoff_t *handoff, uint64_t magic) {
    /* 1. Initialize serial port for early diagnostic output */
    uart_init();

    /* 2. Validate versioned boot handoff from UEFI loader */
    int val_res = boot_validate_handoff(handoff, magic);
    if (val_res != 0) {
        panic("Boot handoff validation failed with code %d\n", val_res);
    }
    /* Switch to kernel higher-half copy of handoff structure */
    handoff = &g_boot_handoff;

    /* 3. Initialize early framebuffer console */
    fb_init(handoff);

    kputs("\n");
    kputs("================================================================================\n");
    kputs("  Stratum Multiprocessor Operating System v0.1.0 (x86-64)\n");
    kputs("================================================================================\n");
    kprintf("[KERNEL] Kernel entered at higher-half entry point\n");
    kprintf("[KERNEL] Boot handoff verified successfully (magic: 0x%lx)\n", magic);

    /* 4. Initialize GDT, TSS, and dedicated IST stacks */
    gdt_init();

    /* 5. Initialize IDT and exception handling */
    idt_init();

    /* 6. Dump system boot configuration */
    boot_dump_info(handoff);

    /* 7. Initialize Physical Memory Manager (PMM) */
    pmm_init(handoff);

    /* 8. Initialize Kernel Object Allocator (SLAB) */
    slab_init();

    /* 9. Initialize Virtual Memory Manager (VMM) and switch CR3 */
    vmm_init(handoff);

    /* Run P04 Memory Verification Tests */
    kprintf("[TEST] Running P04 memory allocation and mapping verification...\n");
    void *p1 = kmalloc(32);
    void *p2 = kmalloc(256);
    void *p3 = kmalloc(4096);
    kassert(p1 != NULL && p2 != NULL && p3 != NULL);
    strcpy((char *)p1, "Stratum slab test");
    kassert(strcmp((char *)p1, "Stratum slab test") == 0);
    kfree(p1);
    kfree(p2);
    kfree(p3);

    /* Test VMM mapping and unmapping */
    uint64_t test_paddr = pmm_alloc_page();
    kassert(test_paddr != 0);
    uint64_t test_vaddr = 0xFFFF900000000000ULL;
    int map_res = vmm_map_page(g_kernel_pml4, test_vaddr, test_paddr, PTE_PRESENT | PTE_WRITABLE);
    kassert(map_res == 0);
    *(volatile uint64_t *)test_vaddr = 0x5354524154554DULL;
    kassert(*(volatile uint64_t *)test_vaddr == 0x5354524154554DULL);
    vmm_unmap_page(g_kernel_pml4, test_vaddr);
    pmm_free_page(test_paddr);

    kprintf("[TEST] P04 memory verification tests passed successfully.\n");

    /* 10. Initialize Multiprocessor Subsystem (SMP) */
    smp_init(handoff);

    /* Run P05 Synchronization and SMP Verification Tests */
    kprintf("[TEST] Running P05 SMP synchronization verification...\n");
    static spinlock_t test_lock = SPINLOCK_INIT;
    spin_lock(&test_lock);
    kassert(!spin_trylock(&test_lock));
    spin_unlock(&test_lock);
    kassert(spin_trylock(&test_lock));
    spin_unlock(&test_lock);

    static mutex_t test_mtx = MUTEX_INIT;
    mutex_lock(&test_mtx);
    kassert(!mutex_trylock(&test_mtx));
    mutex_unlock(&test_mtx);
    kassert(mutex_trylock(&test_mtx));
    mutex_unlock(&test_mtx);

    /* Broadcast cross-CPU TLB shootdown IPI */
    smp_tlb_shootdown(0);

    kprintf("[TEST] P05 SMP synchronization verification passed successfully.\n");
    kprintf("[KERNEL] Phase P05 reached. Entering kernel idle loop with %u CPU(s) online.\n",
            smp_get_online_cpus());

    /* Deliberate fault injection test for P03 verification */
    if (strstr(handoff->cmdline, "fault=ud2")) {
        kprintf("[TEST] Injecting deliberate invalid opcode (#UD) fault...\n");
        __asm__ volatile("ud2");
    }

    /* Kernel main idle loop */
    while (1) {
        hlt();
    }
}
