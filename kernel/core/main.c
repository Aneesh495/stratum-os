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
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/user_elf.h>
#include <kernel/file.h>
#include <kernel/pipe.h>
#include <kernel/process.h>
#include <kernel/x86_64.h>

extern const uint8_t g_user_init_binary[];
extern const uint64_t g_user_init_binary_len;

static volatile uint32_t g_thread_test_counter = 0;
static volatile uint32_t g_threads_completed = 0;
static spinlock_t g_sched_test_lock = SPINLOCK_INIT;

static void worker_priority(void *arg) {
    uint64_t id = (uint64_t)arg;
    for (int i = 0; i < 5; i++) {
        uint64_t flags;
        spin_lock_irqsave(&g_sched_test_lock, &flags);
        g_thread_test_counter++;
        spin_unlock_irqrestore(&g_sched_test_lock, flags);
        thread_yield();
    }
    kprintf("[SCHED] Worker %lu finished (counter=%u)\n", id, g_thread_test_counter);
    __atomic_add_fetch(&g_threads_completed, 1, __ATOMIC_SEQ_CST);
}

static void worker_sleeper(void *arg) {
    uint64_t id = (uint64_t)arg;
    kprintf("[SCHED] Sleeper %lu sleeping for 30 ms...\n", id);
    thread_sleep_ms(30);
    kprintf("[SCHED] Sleeper %lu woke up at uptime %lu ms\n", id, timer_get_uptime_ms());
    __atomic_add_fetch(&g_threads_completed, 1, __ATOMIC_SEQ_CST);
}

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

    /* 11. Initialize Preemptive SMP Scheduler (P06) */
    sched_init();

    kprintf("[TEST] Running P06 preemptive SMP scheduler verification...\n");

    thread_create("worker_rt", worker_priority, (void *)1, THREAD_PRIO_REALTIME);
    thread_create("worker_hi", worker_priority, (void *)2, THREAD_PRIO_HIGH);
    thread_create("worker_norm", worker_priority, (void *)3, THREAD_PRIO_NORMAL);
    thread_create("sleeper", worker_sleeper, (void *)4, THREAD_PRIO_NORMAL);

    sti();
    uint64_t start_ms = timer_get_uptime_ms();
    while (g_threads_completed < 4 && (timer_get_uptime_ms() - start_ms) < 3000) {
        hlt();
    }

    kassert(g_threads_completed == 4);
    kassert(g_thread_test_counter == 15);
    kprintf("[TEST] P06 scheduler verification passed successfully.\n");

    /* 12. Initialize Fast System Call ABI and Safe Usercopy (P07) */
    syscall_init();

    kprintf("[TEST] Running P07 User ABI & Safe Usercopy verification...\n");

    /* Test 1: Verify copy_from_user recovers gracefully from illegal unmapped address */
    char fault_scratch[16];
    int fault_res = copy_from_user(fault_scratch, (const void *)0x0000400000000000ULL, sizeof(fault_scratch));
    kassert(fault_res == -14);
    kprintf("[TEST] P07 safe usercopy fault recovery verified (status=%d).\n", fault_res);

    /* Test 2: Load User ELF binary into isolated address space */
    static user_program_t s_user_prog;
    int elf_res = user_elf_load(g_user_init_binary, g_user_init_binary_len, &s_user_prog);
    kassert(elf_res == 0);
    kassert(s_user_prog.entry_point == 0x400000);
    kassert(s_user_prog.user_stack_top != 0);
    kprintf("[TEST] P07 user ELF64 loader verified.\n");

    /* 13. Initialize Processes, File Descriptors, and IPC (P08) */
    file_init();
    pipe_init();
    process_init();

    kprintf("[TEST] Running P08 Process lifecycle, fork/waitpid, and IPC pipe verification...\n");
    process_t *init_proc = process_create_init(&s_user_prog);
    kassert(init_proc != NULL);
    kassert(init_proc->pid == 1);

    /* Await user process completion */
    uint64_t user_wait_start = timer_get_uptime_ms();
    while (!g_user_init_finished && (timer_get_uptime_ms() - user_wait_start) < 5000) {
        hlt();
    }

    kassert(g_user_init_finished);
    kassert(g_user_exit_code == 42);
    kprintf("[TEST] P08 Processes, Threads, and IPC gates verified successfully.\n");
    kprintf("[KERNEL] Phase P08 reached. Entering kernel idle loop with %u CPU(s) online.\n",
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
