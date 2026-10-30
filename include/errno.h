/* Error numbers shared by the kernel and user programs. System calls return
 * these as negative values. */

#ifndef ERRNO_H
#define ERRNO_H

#define EPERM    1      /* operation not allowed */
#define ENOENT   2      /* no such file or directory */
#define ESRCH    3      /* no such process */
#define EINTR    4      /* interrupted */
#define EIO      5      /* disk error */
#define ENOEXEC  8      /* not a valid program */
#define EBADF    9      /* bad file descriptor */
#define ECHILD  10      /* no child processes */
#define EAGAIN  11      /* try again */
#define ENOMEM  12      /* out of memory */
#define EACCES  13      /* permission denied */
#define EFAULT  14      /* bad user address */
#define EEXIST  17      /* already exists */
#define ENOTDIR 20      /* not a directory */
#define EISDIR  21      /* is a directory */
#define EINVAL  22      /* invalid argument */
#define EMFILE  24      /* too many open files */
#define EFBIG   27      /* file too big */
#define ENOSPC  28      /* disk full */
#define ESPIPE  29      /* can't seek on a pipe */
#define EPIPE   32      /* nobody is reading from the pipe */
#define ENAMETOOLONG 36 /* name too long */
#define ENOSYS  38      /* no such system call */
#define ENOTEMPTY 39    /* directory isn't empty */

#endif
