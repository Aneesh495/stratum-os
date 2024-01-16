/* Stratum Interrupt Service Routine Stubs (x86-64) */
.section .text
.extern exception_dispatch

.macro ISR_NOERR num
.global isr_\num
.type isr_\num, @function
isr_\num:
    pushq $0            /* Dummy error code */
    pushq $\num         /* Vector number */
    jmp common_isr
.endm

.macro ISR_ERR num
.global isr_\num
.type isr_\num, @function
isr_\num:
    pushq $\num         /* Vector number (error code already on stack) */
    jmp common_isr
.endm

/* CPU Exceptions (0..31) */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8             /* Double Fault */
ISR_NOERR 9
ISR_ERR   10            /* Invalid TSS */
ISR_ERR   11            /* Segment Not Present */
ISR_ERR   12            /* Stack-Segment Fault */
ISR_ERR   13            /* General Protection Fault */
ISR_ERR   14            /* Page Fault */
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17            /* Alignment Check */
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21            /* Control Protection */
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29            /* VMM Communication */
ISR_ERR   30            /* Security Exception */
ISR_NOERR 31

/* IRQs and User Interrupts (32..255) */
.altmacro
.macro GENERATE_IRQS from, to
    .set i, \from
    .rept (\to - \from + 1)
        ISR_NOERR %i
        .set i, i + 1
    .endr
.endm

GENERATE_IRQS 32, 255

common_isr:
    /* Clear Direction Flag */
    cld

    /* Push general purpose registers */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* First argument to exception_dispatch: pointer to interrupt_frame_t */
    movq %rsp, %rdi
    call exception_dispatch

    /* Restore general purpose registers */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    /* Clean up vector and error code */
    addq $16, %rsp

    iretq

/* Pointer table for IDT setup */
.section .data
.align 8
.global isr_stub_table
isr_stub_table:
.macro PUSH_STUB_PTR num
    .quad isr_\num
.endm

.set j, 0
.rept 256
    PUSH_STUB_PTR %j
    .set j, j + 1
.endr
