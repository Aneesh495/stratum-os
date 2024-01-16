#ifndef STRATUM_KERNEL_FB_H
#define STRATUM_KERNEL_FB_H

#include <kernel/types.h>
#include <shared/boot_info.h>

void fb_init(const boot_handoff_t *handoff);
void fb_putchar(char c);
void fb_puts(const char *s);
void fb_clear(uint32_t color);

#endif /* STRATUM_KERNEL_FB_H */
