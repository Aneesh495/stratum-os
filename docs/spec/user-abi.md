# Stratum User ABI Specification

Version: 1.0.0
Status: Frozen

## 1. Overview
This document defines the interface boundary between native user space processes and the Stratum operating system kernel.

## 2. Syscall Calling Convention
Native user programs invoke kernel services via the x86-64 `syscall` instruction.

- Syscall Number: passed in register `RAX`.
- Arguments (up to 6): passed in `RDI`, `RSI`, `RDX`, `R10`, `R8`, `R9`.
- Hardware Saved Registers: `RCX` (user RIP) and `R11` (user RFLAGS) are clobbered by hardware.
- Preserved Registers: `RBX`, `RBP`, `RSP`, `R12`, `R13`, `R14`, `R15` are strictly preserved across syscall dispatch.
- Return Value: returned in `RAX`.
  - Non-negative integer (`>= 0`): Success value (bytes transferred, descriptor, PID, or 0).
  - Negative integer (`-1` to `-4095`): Error code representing `-errno` (e.g., `-EINVAL`, `-EBADF`).

## 3. Core Error Numbers (`errno.h`)
```c
#define STRATUM_EOK          0   /* Success */
#define STRATUM_EPERM        1   /* Operation not permitted */
#define STRATUM_ENOENT       2   /* No such file or directory */
#define STRATUM_ESRCH        3   /* No such process */
#define STRATUM_EINTR        4   /* Interrupted system call */
#define STRATUM_EIO          5   /* I/O error */
#define STRATUM_EBADF        9   /* Bad file descriptor */
#define STRATUM_EAGAIN       11  /* Resource temporarily unavailable */
#define STRATUM_ENOMEM       12  /* Cannot allocate memory */
#define STRATUM_EACCES       13  /* Permission denied */
#define STRATUM_EFAULT       14  /* Bad address */
#define STRATUM_EBUSY        16  /* Device or resource busy */
#define STRATUM_EEXIST       17  /* File exists */
#define STRATUM_ENODEV       19  /* No such device */
#define STRATUM_ENOTDIR      20  /* Not a directory */
#define STRATUM_EISDIR       21  /* Is a directory */
#define STRATUM_EINVAL       22  /* Invalid argument */
#define STRATUM_ENFILE       23  /* File table overflow */
#define STRATUM_EMFILE       24  /* Too many open files */
#define STRATUM_ENOSPC       28  /* No space left on device */
#define STRATUM_EPIPE        32  /* Broken pipe */
#define STRATUM_ENOSYS       38  /* Function not implemented */
#define STRATUM_ETIMEDOUT    110 /* Connection timed out */
#define STRATUM_ECONNREFUSED 111 /* Connection refused */
```

## 4. Syscall Table
| Number | Name | Arguments | Description |
| --- | --- | --- | --- |
| 1 | `sys_exit` | `int status` | Terminate current process/thread |
| 2 | `sys_fork` | `void` | Create copy-on-write child process |
| 3 | `sys_execve` | `const char *path, char **argv, char **envp` | Replace address space with ELF binary |
| 4 | `sys_waitpid` | `int pid, int *status, int options` | Wait for child process state change |
| 5 | `sys_getpid` | `void` | Return current process ID |
| 10 | `sys_thread_create` | `void *(*func)(void*), void *arg, void *stack_top` | Spawn native user thread |
| 11 | `sys_thread_exit` | `void *retval` | Terminate calling thread |
| 12 | `sys_thread_join` | `int tid, void **retval` | Wait for user thread termination |
| 13 | `sys_thread_self` | `void` | Return current thread ID |
| 14 | `sys_futex` | `int *uaddr, int op, int val, const struct timespec *timeout` | Fast user-space synchronization |
| 15 | `sys_arch_prctl` | `int code, unsigned long addr` | Set thread architecture state (FS base for TLS) |
| 20 | `sys_mmap` | `void *addr, size_t len, int prot, int flags, int fd, off_t offset` | Map pages into address space |
| 21 | `sys_munmap` | `void *addr, size_t len` | Unmap pages from address space |
| 22 | `sys_mprotect` | `void *addr, size_t len, int prot` | Set protection on memory range |
| 23 | `sys_brk` | `void *addr` | Change process heap break address |
| 30 | `sys_read` | `int fd, void *buf, size_t count` | Read bytes from file descriptor |
| 31 | `sys_write` | `int fd, const void *buf, size_t count` | Write bytes to file descriptor |
| 32 | `sys_open` | `const char *path, int flags, int mode` | Open or create file |
| 33 | `sys_close` | `int fd` | Close file descriptor |
| 34 | `sys_lseek` | `int fd, off_t offset, int whence` | Reposition read/write file offset |
| 35 | `sys_dup2` | `int oldfd, int newfd` | Duplicate file descriptor |
| 36 | `sys_pipe` | `int pipefd[2]` | Create bidirectional communication pipe |
| 37 | `sys_poll` | `struct pollfd *fds, nfds_t nfds, int timeout_ms` | Wait for readiness events on descriptors |
| 40 | `sys_stat` | `const char *path, struct stat *buf` | Get file status |
| 41 | `sys_mkdir` | `const char *path, int mode` | Create directory |
| 42 | `sys_unlink` | `const char *path` | Remove directory entry |
| 43 | `sys_rename` | `const char *oldpath, const char *newpath` | Atomically rename file |
| 44 | `sys_readdir` | `int fd, void *buf, size_t count` | Read directory entries |
| 45 | `sys_fsync` | `int fd` | Synchronize file changes to durable storage |
| 50 | `sys_socket` | `int domain, int type, int protocol` | Create network communication socket |
| 51 | `sys_bind` | `int sockfd, const struct sockaddr *addr, socklen_t addrlen` | Bind socket to local address/port |
| 52 | `sys_listen` | `int sockfd, int backlog` | Listen for incoming connections |
| 53 | `sys_accept` | `int sockfd, struct sockaddr *addr, socklen_t *addrlen` | Accept incoming connection |
| 54 | `sys_connect` | `int sockfd, const struct sockaddr *addr, socklen_t addrlen` | Initiate connection on socket |
| 55 | `sys_send` | `int sockfd, const void *buf, size_t len, int flags` | Send payload on connected socket |
| 56 | `sys_recv` | `int sockfd, void *buf, size_t len, int flags` | Receive payload from connected socket |
| 57 | `sys_shutdown` | `int sockfd, int how` | Shut down socket channels |
| 60 | `sys_clock_gettime`| `int clockid, struct timespec *tp` | Retrieve monotonic or real time |
| 61 | `sys_nanosleep`| `const struct timespec *req, struct timespec *rem` | High-resolution task sleep |
| 70 | `sys_trace_emit` | `uint32_t event_id, uint64_t arg0, uint64_t arg1` | Record structured diagnostic trace event |
| 71 | `sys_sysinfo` | `struct sysinfo *info` | Query system load, memory, tasks |

## 5. Memory Safety and Copying Rules
All pointers received from user space must be validated by the kernel prior to dereference:
1. `validate_user_ptr(ptr, len, write_access)` verifies range lies entirely within `0x0000000000010000` to `0x00007FFFFFFFFFFF`.
2. Safe copy functions (`copy_from_user`, `copy_to_user`, `copy_str_from_user`) use fault-protected assembly routines with exception fixup tables. If a page fault occurs during copy, the copy function safely aborts and returns `-STRATUM_EFAULT` without crashing the kernel.
