/* Stratum Kernel Architecture Entry Point (x86-64) */
.section .text.entry
.global _start
.type _start, @function

_start:
    /* Clear direction flag for string operations */
    cld

    /* Disable maskable interrupts */
    cli

    /* Reset RFLAGS */
    pushq $0
    popfq

    /* Set up kernel stack */
    movabsq $boot_stack_top, %rsp

    /*
     * RDI = physical address of boot_handoff_t
     * RSI = STRATUM_BOOT_MAGIC
     * Call the architecture-independent kernel entry point
     */
    call kmain

    /* Fallback halt loop if kmain returns */
.Lhalt:
    cli
    hlt
    jmp .Lhalt

.size _start, . - _start

.section .bss
.align 16
.global boot_stack
.global boot_stack_top
boot_stack:
    .skip 0x10000 /* 64 KiB kernel boot stack */
boot_stack_top:
