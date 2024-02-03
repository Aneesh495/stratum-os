.section .text
.global sched_context_switch
.global thread_trampoline

/*
 * void sched_context_switch(uint64_t *prev_rsp_ptr, uint64_t next_rsp, uint64_t next_cr3);
 * SysV ABI:
 *   %rdi: prev_rsp_ptr (where to write current %rsp)
 *   %rsi: next_rsp (new %rsp to switch to)
 *   %rdx: next_cr3 (physical CR3 to switch to, or 0 to keep current)
 */
sched_context_switch:
    /* Push callee-saved registers onto current stack */
    pushfq
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Save current stack pointer */
    movq %rsp, (%rdi)

    /* Switch CR3 if specified and different */
    testq %rdx, %rdx
    jz 1f
    movq %cr3, %rax
    cmpq %rax, %rdx
    je 1f
    movq %rdx, %cr3
1:
    /* Switch stack pointer to next thread */
    movq %rsi, %rsp

    /* Pop callee-saved registers from next stack */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    popfq

    /* Return into next thread's saved instruction pointer */
    ret

/*
 * thread_trampoline:
 * Entry point for newly created kernel threads.
 */
thread_trampoline:
    callq sched_finish_switch
    popq %rax       /* Entry function pointer */
    popq %rdi       /* Argument pointer */
    sti
    callq *%rax     /* Invoke entry(arg) */

    /* If thread function returns, call thread_exit */
    callq thread_exit
.Lhalt_loop:
    hlt
    jmp .Lhalt_loop
