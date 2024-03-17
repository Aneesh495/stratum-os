#include <user/strat_api.h>
#include <stdarg.h>

typedef struct {
    int  fd;
    char buf[512];
    int  pos;
} print_buf_t;

static void buf_flush(print_buf_t *pb) {
    if (pb->pos > 0) {
        write(pb->fd, pb->buf, (size_t)pb->pos);
        pb->pos = 0;
    }
}

static void buf_putc(print_buf_t *pb, char c) {
    if (pb->pos >= (int)sizeof(pb->buf) - 1) {
        buf_flush(pb);
    }
    pb->buf[pb->pos++] = c;
    if (c == '\n') {
        buf_flush(pb);
    }
}

static void buf_puts(print_buf_t *pb, const char *s) {
    if (!s) s = "(null)";
    while (*s) {
        buf_putc(pb, *s++);
    }
}

static void buf_print_uint(print_buf_t *pb, uint64_t val, int base) {
    char num_buf[32];
    int pos = 0;
    const char digits[] = "0123456789abcdef";

    if (val == 0) {
        buf_putc(pb, '0');
        return;
    }

    while (val > 0) {
        num_buf[pos++] = digits[val % base];
        val /= base;
    }

    for (int i = pos - 1; i >= 0; i--) {
        buf_putc(pb, num_buf[i]);
    }
}

static void buf_print_int(print_buf_t *pb, int64_t val) {
    if (val < 0) {
        buf_putc(pb, '-');
        val = -val;
    }
    buf_print_uint(pb, (uint64_t)val, 10);
}

void u_dprintf(int fd, const char *fmt, ...) {
    print_buf_t pb;
    pb.fd = fd;
    pb.pos = 0;

    va_list ap;
    va_start(ap, fmt);

    for (size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] == '%' && fmt[i + 1] != '\0') {
            i++;
            if (fmt[i] == 's') {
                const char *s = va_arg(ap, const char *);
                buf_puts(&pb, s);
            } else if (fmt[i] == 'd') {
                int val = va_arg(ap, int);
                buf_print_int(&pb, val);
            } else if (fmt[i] == 'u') {
                unsigned int val = va_arg(ap, unsigned int);
                buf_print_uint(&pb, val, 10);
            } else if (fmt[i] == 'l' && fmt[i + 1] == 'u') {
                i++;
                uint64_t val = va_arg(ap, uint64_t);
                buf_print_uint(&pb, val, 10);
            } else if (fmt[i] == 'l' && fmt[i + 1] == 'd') {
                i++;
                int64_t val = va_arg(ap, int64_t);
                buf_print_int(&pb, val);
            } else if (fmt[i] == 'x') {
                unsigned int val = va_arg(ap, unsigned int);
                buf_print_uint(&pb, val, 16);
            } else if (fmt[i] == 'l' && fmt[i + 1] == 'x') {
                i++;
                uint64_t val = va_arg(ap, uint64_t);
                buf_print_uint(&pb, val, 16);
            } else if (fmt[i] == 'c') {
                char c = (char)va_arg(ap, int);
                buf_putc(&pb, c);
            } else if (fmt[i] == '%') {
                buf_putc(&pb, '%');
            }
        } else {
            buf_putc(&pb, fmt[i]);
        }
    }

    va_end(ap);
    buf_flush(&pb);
}

void u_printf(const char *fmt, ...) {
    print_buf_t pb;
    pb.fd = 1;
    pb.pos = 0;

    va_list ap;
    va_start(ap, fmt);

    for (size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] == '%' && fmt[i + 1] != '\0') {
            i++;
            if (fmt[i] == 's') {
                const char *s = va_arg(ap, const char *);
                buf_puts(&pb, s);
            } else if (fmt[i] == 'd') {
                int val = va_arg(ap, int);
                buf_print_int(&pb, val);
            } else if (fmt[i] == 'u') {
                unsigned int val = va_arg(ap, unsigned int);
                buf_print_uint(&pb, val, 10);
            } else if (fmt[i] == 'l' && fmt[i + 1] == 'u') {
                i++;
                uint64_t val = va_arg(ap, uint64_t);
                buf_print_uint(&pb, val, 10);
            } else if (fmt[i] == 'l' && fmt[i + 1] == 'd') {
                i++;
                int64_t val = va_arg(ap, int64_t);
                buf_print_int(&pb, val);
            } else if (fmt[i] == 'x') {
                unsigned int val = va_arg(ap, unsigned int);
                buf_print_uint(&pb, val, 16);
            } else if (fmt[i] == 'l' && fmt[i + 1] == 'x') {
                i++;
                uint64_t val = va_arg(ap, uint64_t);
                buf_print_uint(&pb, val, 16);
            } else if (fmt[i] == 'c') {
                char c = (char)va_arg(ap, int);
                buf_putc(&pb, c);
            } else if (fmt[i] == '%') {
                buf_putc(&pb, '%');
            }
        } else {
            buf_putc(&pb, fmt[i]);
        }
    }

    va_end(ap);
    buf_flush(&pb);
}

int printf(const char *fmt, ...) {
    print_buf_t pb;
    pb.fd = 1;
    pb.pos = 0;

    va_list ap;
    va_start(ap, fmt);

    for (size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] == '%' && fmt[i + 1] != '\0') {
            i++;
            if (fmt[i] == 's') {
                const char *s = va_arg(ap, const char *);
                buf_puts(&pb, s);
            } else if (fmt[i] == 'd') {
                int val = va_arg(ap, int);
                buf_print_int(&pb, val);
            } else if (fmt[i] == 'u') {
                unsigned int val = va_arg(ap, unsigned int);
                buf_print_uint(&pb, val, 10);
            } else if (fmt[i] == 'l' && fmt[i + 1] == 'u') {
                i++;
                uint64_t val = va_arg(ap, uint64_t);
                buf_print_uint(&pb, val, 10);
            } else if (fmt[i] == 'l' && fmt[i + 1] == 'd') {
                i++;
                int64_t val = va_arg(ap, int64_t);
                buf_print_int(&pb, val);
            } else if (fmt[i] == 'x') {
                unsigned int val = va_arg(ap, unsigned int);
                buf_print_uint(&pb, val, 16);
            } else if (fmt[i] == 'l' && fmt[i + 1] == 'x') {
                i++;
                uint64_t val = va_arg(ap, uint64_t);
                buf_print_uint(&pb, val, 16);
            } else if (fmt[i] == 'c') {
                char c = (char)va_arg(ap, int);
                buf_putc(&pb, c);
            } else if (fmt[i] == '%') {
                buf_putc(&pb, '%');
            }
        } else {
            buf_putc(&pb, fmt[i]);
        }
    }

    va_end(ap);
    buf_flush(&pb);
    return 0;
}
