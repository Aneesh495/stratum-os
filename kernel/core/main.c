#include <kernel/kernel.h>
#include <kernel/boot.h>
#include <kernel/uart.h>
#include <kernel/fb.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/x86_64.h>

void kmain(boot_handoff_t *handoff, uint64_t magic) {
    /* 1. Initialize serial port for early diagnostic output */
    uart_init();

    /* 2. Validate versioned boot handoff from UEFI loader */
    int val_res = boot_validate_handoff(handoff, magic);
    if (val_res != 0) {
        panic("Boot handoff validation failed with code %d\n", val_res);
    }

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

    kprintf("[KERNEL] Phase P03 CPU, traps, and console initialized. Entering kernel loop.\n");

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
