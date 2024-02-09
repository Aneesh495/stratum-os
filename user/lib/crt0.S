.section .text
.global _start
.extern main
.extern exit

_start:
    /* Clear base pointer for clean stack trace */
    xorq %rbp, %rbp

    /* Call main(0, 0) */
    xorl %edi, %edi
    xorl %esi, %esi
    callq main

    /* Call exit(eax) */
    movl %eax, %edi
    callq exit

.Lhalt:
    jmp .Lhalt
