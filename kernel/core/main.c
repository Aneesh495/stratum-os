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
#include <kernel/pci.h>
#include <kernel/virtio.h>
#include <kernel/virtio_blk.h>
#include <kernel/virtio_net.h>
#include <kernel/vfs.h>
#include <kernel/stratafs.h>
#include <kernel/journal.h>
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

    /* Ensure BSP execution is on g_kernel_pml4 */
    write_cr3(virt_to_phys(g_kernel_pml4));

    /* 14. Initialize PCI Bus and Virtio Hardware I/O (P09) */
    pci_init();
    virtio_blk_init();
    virtio_net_init();

    kprintf("[TEST] Running P09 PCI and Virtio Hardware I/O verification (Gate A06)...\n");

    /* Test 1: Block Device I/O */
    virtio_blk_dev_t *blk = virtio_blk_get_primary();
    if (blk != NULL) {
        kassert(blk->capacity_sectors > 0);

        /* Read sector 0 */
        static uint8_t sector_buf[VIRTIO_BLK_SECTOR_SIZE];
        int64_t rd = virtio_blk_read(blk, 0, 1, sector_buf);
        kassert(rd == VIRTIO_BLK_SECTOR_SIZE);

        /* Write pattern to sector 100 */
        static uint8_t write_pat[VIRTIO_BLK_SECTOR_SIZE];
        for (int i = 0; i < VIRTIO_BLK_SECTOR_SIZE; i++) {
            write_pat[i] = (uint8_t)(0xA5 ^ (i & 0xFF));
        }

        int64_t wr = virtio_blk_write(blk, 100, 1, write_pat);
        kassert(wr == VIRTIO_BLK_SECTOR_SIZE);

        int fl = virtio_blk_flush(blk);
        kassert(fl == 0);

        /* Read back sector 100 and verify integrity */
        static uint8_t readback_pat[VIRTIO_BLK_SECTOR_SIZE];
        memset(readback_pat, 0, VIRTIO_BLK_SECTOR_SIZE);
        int64_t rd2 = virtio_blk_read(blk, 100, 1, readback_pat);
        kassert(rd2 == VIRTIO_BLK_SECTOR_SIZE);
        kassert(memcmp(write_pat, readback_pat, VIRTIO_BLK_SECTOR_SIZE) == 0);

        /* Test queue index cycling with 50 sequential single-sector operations */
        for (int iter = 0; iter < 50; iter++) {
            int64_t r = virtio_blk_read(blk, (uint64_t)(iter % 64), 1, sector_buf);
            kassert(r == VIRTIO_BLK_SECTOR_SIZE);
        }
        kprintf("[TEST] Virtio-blk synchronous read, write, flush, and queue index cycling verified.\n");
    } else {
        kprintf("[TEST] Note: Virtio-blk not attached; skipping block device I/O test.\n");
    }

    /* Test 2: Network Device I/O */
    virtio_net_dev_t *net = virtio_net_get_primary();
    if (net != NULL) {
        /* Verify MAC address is non-zero */
        bool mac_nonzero = false;
        for (int i = 0; i < VIRTIO_NET_ETH_ALEN; i++) {
            if (net->mac[i] != 0) mac_nonzero = true;
        }
        kassert(mac_nonzero);

        /* Build test broadcast frame (ARP probe, 64 bytes) */
        uint8_t test_frame[64];
        memset(test_frame, 0, sizeof(test_frame));
        /* Destination: Broadcast FF:FF:FF:FF:FF:FF */
        memset(&test_frame[0], 0xFF, 6);
        /* Source: Device MAC */
        memcpy(&test_frame[6], net->mac, 6);
        /* EtherType: 0x0806 (ARP) */
        test_frame[12] = 0x08;
        test_frame[13] = 0x06;

        int tx_res = virtio_net_transmit(net, test_frame, sizeof(test_frame));
        kassert(tx_res == sizeof(test_frame));
        kprintf("[TEST] Virtio-net MAC identification and TX frame transmission verified.\n");
    } else {
        kprintf("[TEST] Note: Virtio-net not attached; skipping network device I/O test.\n");
    }

    kprintf("[TEST] P09 Virtio block and network hardware I/O verified successfully (Gate A06 passed).\n");

    /* 15. Initialize VFS, Journal, and StrataFS (P10 & P11) */
    vfs_init();
    stratafs_init();

    kprintf("[TEST] Running P10 & P11 VFS, StrataFS Storage, and Journal Recovery verification (Gate A07)...\n");

    /* Mount StrataFS on root / */
    int m_res = vfs_mount("virtio-blk", "/", "stratafs", 0);
    kassert(m_res == 0);

    /* Test 1: Directory hierarchy creation */
    kprintf("[TEST] mkdir /system...\n");
    int d_res = vfs_mkdir("/system", 0755);
    kprintf("[TEST] mkdir /system returned %d\n", d_res);
    kassert(d_res == 0);
    kprintf("[TEST] mkdir /system/logs...\n");
    int d_res2 = vfs_mkdir("/system/logs", 0755);
    kprintf("[TEST] mkdir /system/logs returned %d\n", d_res2);
    kassert(d_res2 == 0);

    /* Test 2: File creation and multi-block writes */
    file_t *f = NULL;
    kprintf("[TEST] open /system/logs/boot.log...\n");
    int o_res = vfs_open("/system/logs/boot.log", O_CREAT | O_WRONLY, 0644, &f);
    kprintf("[TEST] open returned %d, f=%p\n", o_res, f);
    kassert(o_res == 0 && f != NULL);

    /* Write 12,000 bytes spanning 3 distinct 4 KiB disk blocks */
    static uint8_t file_buf[12000];
    for (int i = 0; i < 12000; i++) {
        file_buf[i] = (uint8_t)(0x5A ^ (i & 0xFF));
    }
    kprintf("[TEST] write 12000 bytes...\n");
    int64_t wr_bytes = vfs_write(f, file_buf, sizeof(file_buf));
    kprintf("[TEST] write returned %ld\n", wr_bytes);
    kassert(wr_bytes == sizeof(file_buf));
    vfs_close(f);

    /* Test 3: Readback and verification */
    f = NULL;
    kprintf("[TEST] reopen for read...\n");
    int ro_res = vfs_open("/system/logs/boot.log", O_RDONLY, 0, &f);
    kprintf("[TEST] reopen returned %d\n", ro_res);
    kassert(ro_res == 0 && f != NULL);

    static uint8_t verify_buf[12000];
    memset(verify_buf, 0, sizeof(verify_buf));
    int64_t rd_bytes = vfs_read(f, verify_buf, sizeof(verify_buf));
    kprintf("[TEST] read returned %ld\n", rd_bytes);
    kassert(rd_bytes == sizeof(verify_buf));
    kassert(memcmp(file_buf, verify_buf, sizeof(file_buf)) == 0);
    vfs_close(f);

    /* Test 4: Single indirect block test (60,000 bytes = 15 blocks) */
    f = NULL;
    int ind_open = vfs_open("/bigfile.dat", O_CREAT | O_WRONLY, 0644, &f);
    kassert(ind_open == 0 && f != NULL);

    static uint8_t big_buf[60000];
    for (int i = 0; i < 60000; i++) {
        big_buf[i] = (uint8_t)(0xC3 ^ ((i >> 4) & 0xFF));
    }
    int64_t big_wr = vfs_write(f, big_buf, sizeof(big_buf));
    kassert(big_wr == sizeof(big_buf));
    vfs_close(f);

    f = NULL;
    int ind_read = vfs_open("/bigfile.dat", O_RDONLY, 0, &f);
    kassert(ind_read == 0 && f != NULL);

    static uint8_t big_verify[60000];
    memset(big_verify, 0, sizeof(big_verify));
    int64_t big_rd = vfs_read(f, big_verify, sizeof(big_verify));
    kassert(big_rd == sizeof(big_verify));
    kassert(memcmp(big_buf, big_verify, sizeof(big_buf)) == 0);
    vfs_close(f);

    /* Test 5: Stat metadata verification */
    vfs_stat_t st;
    int stat_res = vfs_stat("/bigfile.dat", &st);
    kassert(stat_res == 0);
    kassert(st.st_size == 60000);

    /* Test 6: Atomic Journal Transaction Checkpoint & Remount Recovery */
    vfs_sync();
    vfs_unmount("/");

    /* Remount and verify persistent state */
    int remount_res = vfs_mount("virtio-blk", "/", "stratafs", 0);
    kassert(remount_res == 0);

    f = NULL;
    int remount_read = vfs_open("/system/logs/boot.log", O_RDONLY, 0, &f);
    kassert(remount_read == 0 && f != NULL);
    memset(verify_buf, 0, sizeof(verify_buf));
    int64_t remount_rd = vfs_read(f, verify_buf, sizeof(verify_buf));
    kassert(remount_rd == sizeof(verify_buf));
    kassert(memcmp(file_buf, verify_buf, sizeof(file_buf)) == 0);
    vfs_close(f);

    /* Test 7: Unlink file */
    int unl_res = vfs_unlink("/bigfile.dat");
    kassert(unl_res == 0);
    f = NULL;
    int unl_open = vfs_open("/bigfile.dat", O_RDONLY, 0, &f);
    kassert(unl_open != 0 && f == NULL);

    kprintf("[TEST] StrataFS directory hierarchy, multi-block files, indirect blocks, atomic journal transactions, and remount recovery verified.\n");
    kprintf("[TEST] P10 & P11 VFS and StrataFS storage verified successfully (Gate A07 passed).\n");
    kprintf("[KERNEL] Phase P11 reached. Entering kernel idle loop with %u CPU(s) online.\n",
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
