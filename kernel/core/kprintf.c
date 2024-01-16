#include <kernel/kernel.h>
#include <kernel/uart.h>
#include <kernel/fb.h>
#include <kernel/x86_64.h>
#include <stdarg.h>

void kputchar(char c) {
    uart_putc(c);
    fb_putchar(c);
}

void kputs(const char *s) {
    if (!s) return;
    while (*s) {
        kputchar(*s++);
    }
}

int kprintf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    kputs(buf);
    return len;
}

void panic(const char *fmt, ...) {
    cli();

    kputs("\n=======================================================\n");
    kputs("KERNEL PANIC: ");

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    kputs(buf);
    kputs("\nSystem halted.\n");
    kputs("=======================================================\n");

    while (1) {
        hlt();
    }
}
