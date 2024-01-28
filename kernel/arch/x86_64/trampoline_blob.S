.section .rodata
.global g_trampoline_binary
.global g_trampoline_binary_len

g_trampoline_binary:
    .incbin "build/trampoline.bin"
g_trampoline_binary_end:

g_trampoline_binary_len:
    .quad g_trampoline_binary_end - g_trampoline_binary
