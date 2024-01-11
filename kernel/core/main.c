#include <kernel/kernel.h>
#include <kernel/boot.h>
#include <kernel/uart.h>
#include <kernel/x86_64.h>

void kmain(boot_handoff_t *handoff, uint64_t magic) {
    /* 1. Initialize serial port for diagnostic output */
    uart_init();

    kputs("\n");
    kputs("================================================================================\n");
    kputs("  Stratum Multiprocessor Operating System v0.1.0 (x86-64)\n");
    kputs("================================================================================\n");
    kprintf("[KERNEL] Kernel entered at higher-half entry point\n");

    /* 2. Validate versioned boot handoff from UEFI loader */
    int val_res = boot_validate_handoff(handoff, magic);
    if (val_res != 0) {
        panic("Boot handoff validation failed with code %d\n", val_res);
    }
    kprintf("[KERNEL] Boot handoff verified successfully (magic: 0x%lx)\n", magic);

    /* 3. Dump system boot configuration */
    boot_dump_info(handoff);

    kprintf("[KERNEL] Boot phase P02 reached successfully. Entering kernel idle loop.\n");

    /* Kernel main idle loop */
    while (1) {
        hlt();
    }
}
