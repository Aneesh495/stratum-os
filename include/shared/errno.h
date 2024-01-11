#ifndef STRATUM_SHARED_ERRNO_H
#define STRATUM_SHARED_ERRNO_H

#define STRATUM_EOK          0   /* Success */
#define STRATUM_EPERM        1   /* Operation not permitted */
#define STRATUM_ENOENT       2   /* No such file or directory */
#define STRATUM_ESRCH        3   /* No such process */
#define STRATUM_EINTR        4   /* Interrupted system call */
#define STRATUM_EIO          5   /* I/O error */
#define STRATUM_ENXIO        6   /* No such device or address */
#define STRATUM_E2BIG        7   /* Argument list too long */
#define STRATUM_ENOEXEC      8   /* Exec format error */
#define STRATUM_EBADF        9   /* Bad file descriptor */
#define STRATUM_ECHILD       10  /* No child processes */
#define STRATUM_EAGAIN       11  /* Resource temporarily unavailable */
#define STRATUM_ENOMEM       12  /* Cannot allocate memory */
#define STRATUM_EACCES       13  /* Permission denied */
#define STRATUM_EFAULT       14  /* Bad address */
#define STRATUM_EBUSY        16  /* Device or resource busy */
#define STRATUM_EEXIST       17  /* File exists */
#define STRATUM_EXDEV        18  /* Cross-device link */
#define STRATUM_ENODEV       19  /* No such device */
#define STRATUM_ENOTDIR      20  /* Not a directory */
#define STRATUM_EISDIR       21  /* Is a directory */
#define STRATUM_EINVAL       22  /* Invalid argument */
#define STRATUM_ENFILE       23  /* File table overflow */
#define STRATUM_EMFILE       24  /* Too many open files */
#define STRATUM_ENOTTY       25  /* Not a typewriter */
#define STRATUM_EFBIG        27  /* File too large */
#define STRATUM_ENOSPC       28  /* No space left on device */
#define STRATUM_ESPIPE       29  /* Illegal seek */
#define STRATUM_EROFS        30  /* Read-only file system */
#define STRATUM_EMLINK       31  /* Too many links */
#define STRATUM_EPIPE        32  /* Broken pipe */
#define STRATUM_ERANGE       34  /* Math result not representable */
#define STRATUM_EDEADLK      35  /* Resource deadlock avoided */
#define STRATUM_ENAMETOOLONG 36  /* File name too long */
#define STRATUM_ENOSYS       38  /* Function not implemented */
#define STRATUM_ENOTEMPTY    39  /* Directory not empty */
#define STRATUM_ELOOP        40  /* Too many levels of symbolic links */
#define STRATUM_ENOBUFS      105 /* No buffer space available */
#define STRATUM_EISCONN      106 /* Transport endpoint is already connected */
#define STRATUM_ENOTCONN     107 /* Transport endpoint is not connected */
#define STRATUM_ETIMEDOUT    110 /* Connection timed out */
#define STRATUM_ECONNREFUSED 111 /* Connection refused */

#endif /* STRATUM_SHARED_ERRNO_H */
