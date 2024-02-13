#ifndef STRAT_API_H
#define STRAT_API_H

#include <stdint.h>
#include <stddef.h>
#include <shared/syscall_nums.h>

#define POLLIN      0x0001
#define POLLPRI     0x0002
#define POLLOUT     0x0004
#define POLLERR     0x0008
#define POLLHUP     0x0010
#define POLLNVAL    0x0020

struct pollfd {
    int      fd;
    int16_t  events;
    int16_t  revents;
};

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

static inline int read(int fd, void *buf, size_t count) {
    return (int)syscall3(SYS_read, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

static inline int close(int fd) {
    return (int)syscall1(SYS_close, (uint64_t)fd);
}

static inline int dup2(int oldfd, int newfd) {
    return (int)syscall2(SYS_dup2, (uint64_t)oldfd, (uint64_t)newfd);
}

static inline int pipe(int pipefd[2]) {
    return (int)syscall1(SYS_pipe, (uint64_t)pipefd);
}

static inline int poll(struct pollfd *fds, uint32_t nfds, int timeout) {
    return (int)syscall3(SYS_poll, (uint64_t)fds, (uint64_t)nfds, (uint64_t)timeout);
}

static inline int fork(void) {
    return (int)syscall0(SYS_fork);
}

static inline int waitpid(int pid, int *status, int options) {
    return (int)syscall3(SYS_waitpid, (uint64_t)pid, (uint64_t)status, (uint64_t)options);
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
