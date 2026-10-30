// Process Management: PCBs, scheduling and wait queues (owner: Process).

#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>
#include <stdint.h>

#include "abi.h"
#include "hal.h"

class File;
class Inode;

#define NUM_PRIORITIES 4  // 0 = highest, 3 = lowest
#define DEFAULT_PRIORITY 2

#define TIME_QUANTUM_TICKS 2  // 2 ticks of 10 ms = 20 ms
#define AGING_LIMIT_TICKS  100  // 1 second in a ready queue moves a process up

enum process_state {
    PROCESS_NEW,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_WAITING,
    PROCESS_TERMINATED,
};

struct Process;

// A line of processes. It is also used for the ready queues.
class WaitQueue {
public:
    void add(Process *process);
    Process *remove_first();
    void sleep();
    void wake_one();
    void wake_all();
    bool is_empty() const;

    Process *head = nullptr;
    Process *tail = nullptr;
};

struct Process {
    int pid;
    int parent_pid;
    int uid;
    int gid;
    char name[PROCESS_NAME_LENGTH];

    enum process_state state;
    int exit_status;

    int priority;  // may rise while waiting, because of aging
    int base_priority;
    uint32_t ready_since;  // tick when it joined a ready queue
    uint32_t wake_tick;  // tick to wake at, if it called thread_sleep()
    uint32_t cpu_ticks;

    // The register snapshot is pushed on the kernel stack at each switch.
    uint32_t saved_esp;
    void *kernel_stack;
    uint32_t kernel_stack_top;

    uint32_t *page_dir;
    uint32_t heap_end;  // current_process program break

    File **files;  // per-process file descriptor table
    Inode *cwd;
    char cwd_path[MAX_PATH];

    void (*entry)(void *);
    void *argument;

    Process *queue_next;  // link in a ready queue or wait queue
    Process *list_next;  // link in the list of all processes
};

extern Process *current_process;

// Scheduler
void scheduler_init();
void schedule();
void scheduler_tick();
void switch_process_if_needed();
void give_up_cpu_if_needed();  // lets a long kernel loop give up the CPU
void idle_loop() __attribute__((noreturn));

// Kernel threads
Process *thread_create(const char *name, int priority,
                          void (*entry)(void *), void *argument);
void thread_exit(int status) __attribute__((noreturn));
int thread_join(Process *thread);
void thread_sleep(uint32_t ticks);

// Pieces of thread creation that fork() also needs
Process *create_process(const char *name, int priority);
void delete_process(Process *process);
void kernel_stack_alloc(Process *process);
void add_new_process(Process *process);
Process *process_list();

// User processes
void process_init();
void process_start_first(const char *path) __attribute__((noreturn));
int process_exec(const char *path, char *const argv[], Registers *r);
int process_fork(const Registers *r);
void process_exit(int status) __attribute__((noreturn));
int process_wait(int *status);
void process_fault(const Registers *r, const char *reason) __attribute__((noreturn));

#endif
