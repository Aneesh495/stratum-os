#ifndef STRATUM_KERNEL_PIPE_H
#define STRATUM_KERNEL_PIPE_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/file.h>
#include <kernel/spinlock.h>
#include <kernel/sched.h>

#define PIPE_CAPACITY 4096

typedef struct pipe {
    spinlock_t   lock;
    char        *buffer;
    size_t       read_idx;
    size_t       write_idx;
    size_t       count;
    uint32_t     readers;
    uint32_t     writers;
    wait_queue_t read_wait;
    wait_queue_t write_wait;
} pipe_t;

void pipe_init(void);
int  pipe_create(file_t **read_file, file_t **write_file);

#endif /* STRATUM_KERNEL_PIPE_H */
