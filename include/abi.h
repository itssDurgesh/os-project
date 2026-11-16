// Everything the kernel and user programs must agree on: system call numbers,
// flags and the structures passed between them (owner: Integration).

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

#define SYS_SETUID      43
#define SYS_SETGID      44
#define SYS_SET_ECHO    45
#define SYS_PROCINFO    46
#define SYS_CHMOD       47

#define SYS_SET_QUOTA   48

#define SYS_MEMINFO     49

#define SYS_PROCMEM     50      // the memory and resources of one process
#define SYS_PROCFILE    51      // one open file of one process
#define SYS_MEMREPORT   52      // print memory reports when programs start and end
#define SYS_SHUTDOWN    53      // turn the computer off

#define SYSCALL_COUNT   54

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

#define SIGINT  2       // Ctrl+C
#define SIGKILL 9       // can not be caught or ignored
#define SIGUSR1 10      // free for programs to use
#define SIGTERM 15      // please stop
#define SIGCHLD 17      // a child process finished
#define NUM_SIGNALS 32

#define SIG_DFL 0       // use the default action
#define SIG_IGN 1       // ignore the signal

#define RESOURCE_PIPE 0
#define RESOURCE_MESSAGE_QUEUE 1
#define RESOURCE_SHARED_MEMORY 2
#define RESOURCE_TYPES 3

#define MAX_MESSAGE_SIZE 256

struct MemoryInfo {
    uint32_t total_frames;
    uint32_t free_frames;
    uint32_t swap_slots_total;
    uint32_t swap_slots_used;
    uint32_t page_faults;
    uint32_t evicted_pages;
};

struct MouseEvent {
    int x_move;
    int y_move;
    int buttons;
};

#define PROCESS_NAME_LENGTH 32

struct procinfo {
    int pid;
    int parent_pid;
    int uid;
    int state;
    int priority;
    uint32_t cpu_ticks;
    char name[PROCESS_NAME_LENGTH];
};

// What one part of a process's memory is used for.
#define REGION_CODE   1         // program instructions, read-only
#define REGION_DATA   2         // global variables
#define REGION_HEAP   3         // malloc() memory, grows with brk/sbrk
#define REGION_STACK  4         // local variables and function calls
#define REGION_MMAP   5         // extra memory asked for with mmap()
#define REGION_SHARED 6         // shared memory, also seen by other processes

#define MAX_REGIONS 20          // 16 memory areas + 4 shared segments

struct memregion {
    uint32_t kind;              // REGION_ value
    uint32_t start;
    uint32_t end;               // first address after the region
    uint32_t writable;
    uint32_t pages_in_ram;
    uint32_t pages_in_swap;
};

// Everything the kernel has given to one process: memory and resources.
struct procmem {
    int pid;
    int parent_pid;
    int uid;
    int state;
    char name[PROCESS_NAME_LENGTH];

    uint32_t region_count;
    struct memregion regions[MAX_REGIONS];

    uint32_t kernel_stack_bytes;   // the stack the kernel uses for this process
    uint32_t page_table_pages;     // page directory and page tables
    uint32_t pcb_bytes;            // the process control block itself

    uint32_t open_files;
    uint32_t shared_segments;
    uint32_t cpu_quota;            // ticks per second, 0 = no limit
    uint32_t pending_signals;      // one bit per signal

    int resources_held[RESOURCE_TYPES];    // Banker's Algorithm
    int resources_maximum[RESOURCE_TYPES];
    int resources_total[RESOURCE_TYPES];   // for the whole system
    int resources_available[RESOURCE_TYPES];
};

#define FILE_NAME_LENGTH 48

#define FILE_KIND_FILE      1
#define FILE_KIND_DIRECTORY 2
#define FILE_KIND_DEVICE    3
#define FILE_KIND_PIPE      4

// One open file of a process, for lsof.
struct fileinfo {
    int fd;
    uint32_t kind;              // FILE_KIND_ value
    uint32_t flags;             // O_ flags it was opened with
    uint32_t offset;            // where the next read or write happens
    uint32_t size;
    uint32_t inode;
    uint32_t shared_by;         // how many descriptors point at this open file
    char name[FILE_NAME_LENGTH];
};

#endif
