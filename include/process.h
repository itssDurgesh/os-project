// Process Management: PCBs, scheduling and wait queues (owner: Process).

#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>
#include <stdint.h>

#include "abi.h"
#include "hal.h"
#include "memory.h"

class File;
class Inode;

#define NUM_PRIORITIES 4  // 0 = highest, 3 = lowest
#define DEFAULT_PRIORITY 2

#define MAX_ATTACHED_SEGMENTS 4

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
    void sleep(bool can_be_interrupted = false);
    bool remove(Process *process);
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
    uint32_t heap_end;  // the current top of the heap
    int heap_area;  // which entry of `areas` is the heap
    uint32_t next_mmap_address;
    MemoryArea areas[MAX_MEMORY_AREAS];

    File **files;  // per-process file descriptor table
    Inode *cwd;
    char cwd_path[MAX_PATH];

    // Signals
    uint32_t pending_signals;  // one bit for each signal waiting to be delivered
    uint32_t signal_handlers[NUM_SIGNALS];
    uint32_t signal_return_address;
    WaitQueue *sleeping_in;  // the queue it is asleep in, if any
    bool can_be_interrupted;  // can a signal wake it up?

    // Shared memory: segment id + 1, or 0 for an empty slot
    int attached_segments[MAX_ATTACHED_SEGMENTS];

    // Banker's Algorithm
    bool banker_declared;
    bool banker_finished;
    int banker_maximum[RESOURCE_TYPES];
    int banker_allocated[RESOURCE_TYPES];

    // CPU quota: the most ticks allowed each second. 0 means no limit.
    uint32_t cpu_quota;
    uint32_t ticks_this_second;

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
Process *find_process(int pid);
void wake_process_for_signal(Process *process);

// User processes
void process_init();
void process_start_first(const char *path) __attribute__((noreturn));
int process_exec(const char *path, char *const argv[], Registers *r);
int process_fork(const Registers *r);
void process_exit(int status) __attribute__((noreturn));
int process_wait(int *status);
void process_fault(const Registers *r, const char *reason) __attribute__((noreturn));

// Signals
bool signal_pending();
int send_signal(Process *target, int signal_number);
void send_signal_to_all_programs(int signal_number);
void deliver_signals(Registers *r);

// Shared memory and Banker's Algorithm clean-up
void shared_memory_detach_all(Process *process);
void shared_memory_copy_to_child(Process *parent, Process *child);
void banker_process_exit(Process *process);

#endif
