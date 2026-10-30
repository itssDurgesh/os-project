/* Everything the kernel and user programs must agree on: system call numbers,
 * flags and the structures passed between them (owner: Integration). */

#ifndef ABI_H
#define ABI_H

#include <stdint.h>

#define SYS_FORK        1
#define SYS_EXEC        2
#define SYS_EXIT        3
#define SYS_WAIT        4
#define SYS_GETPID      5
#define SYS_KILL        6
#define SYS_SLEEP       7
#define SYS_SIGACTION   8
#define SYS_SIGRETURN   9
#define SYS_MMAP        10
#define SYS_MUNMAP      11
#define SYS_BRK         12
#define SYS_SBRK        13
#define SYS_OPEN        14
#define SYS_CLOSE       15
#define SYS_READ        16
#define SYS_WRITE       17
#define SYS_SEEK        18
#define SYS_STAT        19
#define SYS_UNLINK      20
#define SYS_MKDIR       21
#define SYS_RMDIR       22
#define SYS_READDIR     23
#define SYS_CHDIR       24
#define SYS_GETCWD      25
#define SYS_DUP2        26
#define SYS_PIPE        27
#define SYS_MSGGET      28
#define SYS_MSGSND      29
#define SYS_MSGRCV      30
#define SYS_SHMGET      31
#define SYS_SHMAT       32
#define SYS_SHMDT       33
#define SYS_SEM_OPEN    34
#define SYS_SEM_WAIT    35
#define SYS_SEM_POST    36
#define SYS_SEM_CLOSE   37
#define SYS_GETUID      38
#define SYS_GETGID      39
#define SYS_RES_DECLARE 40
#define SYS_RES_REQUEST 41
#define SYS_RES_RELEASE 42

#define SYSCALL_COUNT   43

#define MAX_FILE_NAME 255
#define MAX_PATH      256
#define MAX_ARGUMENTS 32

#define O_RDONLY 0x0
#define O_WRONLY 0x1
#define O_RDWR   0x2
#define O_ACCESS_MASK 0x3
#define O_CREAT  0x40
#define O_TRUNC  0x200
#define O_APPEND 0x400

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define TYPE_FILE      1
#define TYPE_DIRECTORY 2
#define TYPE_DEVICE    3

struct dirent {
    uint32_t inode;
    uint32_t type;
    char name[MAX_FILE_NAME + 1];
};

struct stat {
    uint32_t inode;
    uint32_t type;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t links;
    uint32_t size;
};

#endif
