#ifndef STRAT_API_H
#define STRAT_API_H

#include <stdint.h>
#include <stddef.h>
#include <shared/syscall_nums.h>

static inline int64_t syscall0(uint64_t nr) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(nr) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall1(uint64_t nr, uint64_t a1) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall2(uint64_t nr, uint64_t a1, uint64_t a2) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall3(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

void exit(int status) __attribute__((noreturn));

static inline int write(int fd, const void *buf, size_t count) {
    return (int)syscall3(SYS_write, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

static inline int getpid(void) {
    return (int)syscall0(SYS_getpid);
}

static inline int yield(void) {
    return (int)syscall0(SYS_thread_create);
}

static inline int sleep_ms(uint32_t ms) {
    return (int)syscall1(SYS_nanosleep, (uint64_t)ms);
}

static inline uint64_t uptime(void) {
    return (uint64_t)syscall0(SYS_clock_gettime);
}

#endif /* STRAT_API_H */
