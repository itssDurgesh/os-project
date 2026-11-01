// Round-Robin scheduler with four priority levels and aging.
// Ready queues are also changed by the timer interrupt and by drivers waking a
// process, so every change to them runs with interrupts off.

#include <stddef.h>

#include "console.h"
#include "hal.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"

#define KERNEL_STACK_PAGES 2
#define KERNEL_STACK_SIZE  (KERNEL_STACK_PAGES * PAGE_SIZE)
#define AGING_CHECK_TICKS  10

#define IDLE_PRIORITY NUM_PRIORITIES  // below every ready queue

extern "C" void switch_stacks(uint32_t *saved_esp, uint32_t next_esp);

Process *current_process;

static WaitQueue ready_queues[NUM_PRIORITIES];
static WaitQueue sleeping;  // threads inside thread_sleep()
static WaitQueue over_quota;  // processes that used up their CPU quota
static WaitQueue exited;  // threads waiting in thread_join()
static SlabCache pcb_cache;
static Process *idle_process;
static Process *all_processes;
static int next_pid = 1;
static int quantum_left;
static volatile bool need_resched;

// Linked queues

void WaitQueue::add(Process *process)
{
    process->queue_next = nullptr;
    if (tail != nullptr)
        tail->queue_next = process;
    else
        head = process;
    tail = process;
}

Process *WaitQueue::remove_first()
{
    Process *process = head;

    if (process != nullptr) {
        head = process->queue_next;
        if (head == nullptr)
            tail = nullptr;
        process->queue_next = nullptr;
    }
    return process;
}

static void make_ready(Process *process)
{
    process->sleeping_in = nullptr;
    process->state = PROCESS_READY;
    process->ready_since = get_ticks();
    ready_queues[process->priority].add(process);

    if (process->priority < current_process->priority)
        need_resched = true;
}

static Process *pick_next()
{
    for (int priority = 0; priority < NUM_PRIORITIES; priority++) {
        Process *process = ready_queues[priority].remove_first();

        if (process != nullptr)
            return process;
    }
    return idle_process;
}

// Scheduling

void schedule()
{
    uint32_t flags = turn_off_interrupts();
    Process *previous = current_process;
    Process *next;

    need_resched = false;

    if (previous->state == PROCESS_RUNNING && previous != idle_process) {
        previous->priority = previous->base_priority;
        make_ready(previous);
    }

    next = pick_next();
    next->state = PROCESS_RUNNING;
    quantum_left = TIME_QUANTUM_TICKS;

    if (next != previous) {
        current_process = next;
        tss_set_kernel_stack(next->kernel_stack_top);
        if (next->page_dir != previous->page_dir)
            paging_switch(next->page_dir);
        switch_stacks(&previous->saved_esp, next->saved_esp);
    }

    restore_interrupts(flags);
}

// Moves any process that has waited too long up one priority level.
static void age_ready_queues(uint32_t now)
{
    for (int priority = 1; priority < NUM_PRIORITIES; priority++) {
        WaitQueue *queue = &ready_queues[priority];
        WaitQueue kept;
        Process *process;

        while ((process = queue->remove_first()) != nullptr) {
            if (now - process->ready_since >= AGING_LIMIT_TICKS) {
                process->priority = priority - 1;
                make_ready(process);
            } else {
                kept.add(process);
            }
        }
        *queue = kept;
    }
}

static void wake_sleepers(uint32_t now)
{
    WaitQueue still_sleeping;
    Process *process;

    while ((process = sleeping.remove_first()) != nullptr) {
        if (now - process->wake_tick < 0x80000000u)
            make_ready(process);
        else
            still_sleeping.add(process);
    }
    sleeping = still_sleeping;
}

// Called from the timer interrupt, every 10 ms.
void scheduler_tick()
{
    uint32_t now = get_ticks();

    if (current_process == nullptr)
        return;

    current_process->cpu_ticks++;
    current_process->ticks_this_second++;
    wake_sleepers(now);

    if (now % TIMER_HZ == 0) {
        for (Process *process = all_processes; process != nullptr;
             process = process->list_next)
            process->ticks_this_second = 0;
        over_quota.wake_all();
    }

    if (current_process->cpu_quota != 0 &&
        current_process->ticks_this_second >= current_process->cpu_quota)
        need_resched = true;

    if (now % AGING_CHECK_TICKS == 0)
        age_ready_queues(now);

    if (current_process == idle_process) {
        need_resched = true;
    } else if (--quantum_left <= 0) {
        need_resched = true;
    }
}

void switch_process_if_needed()
{
    // A process over its CPU quota waits for the next second.
    while (current_process->cpu_quota != 0 &&
           current_process->ticks_this_second >= current_process->cpu_quota)
        over_quota.sleep();

    if (need_resched)
        schedule();
}

void give_up_cpu_if_needed()
{
    if (need_resched)
        schedule();
}

void idle_loop()
{
    for (;;) {
        if (need_resched)
            schedule();
        enable_interrupts_and_wait();
    }
}

// Kernel threads

Process *create_process(const char *name, int priority)
{
    Process *process = (Process *)slab_alloc(&pcb_cache);

    if (process == nullptr)
        return nullptr;

    memset(process, 0, sizeof(*process));
    process->pid = next_pid++;
    process->parent_pid = (current_process != nullptr) ? current_process->pid : 0;
    process->state = PROCESS_NEW;
    process->priority = priority;
    process->base_priority = priority;
    process->page_dir = kernel_page_dir;
    copy_string(process->name, name, PROCESS_NAME_LENGTH);

    process->list_next = all_processes;
    all_processes = process;
    return process;
}

// Frees a finished process. Nothing may run on its stack any more.
void delete_process(Process *process)
{
    Process **link;

    for (link = &all_processes; *link != nullptr; link = &(*link)->list_next) {
        if (*link == process) {
            *link = process->list_next;
            break;
        }
    }

    if (process->kernel_stack != nullptr)
        free_frames(virt_to_phys(process->kernel_stack));
    slab_free(process);
}

// Returns false if there is no memory for the stack.
bool kernel_stack_alloc(Process *process)
{
    uint32_t frames = alloc_frames(KERNEL_STACK_PAGES);

    if (frames == 0)
        return false;

    process->kernel_stack = phys_to_virt(frames);
    process->kernel_stack_top = (uint32_t)process->kernel_stack + KERNEL_STACK_SIZE;
    return true;
}

void add_new_process(Process *process)
{
    uint32_t flags = turn_off_interrupts();

    make_ready(process);
    restore_interrupts(flags);
}

Process *process_list()
{
    return all_processes;
}

int process_count()
{
    int count = 0;

    for (Process *process = all_processes; process != nullptr;
         process = process->list_next)
        count++;

    return count;
}

Process *find_process(int pid)
{
    for (Process *process = all_processes; process != nullptr;
         process = process->list_next) {
        if (process->pid == pid)
            return process;
    }
    return nullptr;
}

// A signal arrived, so stop waiting. The sleeper sees signal_pending() and gives up.
void wake_process_for_signal(Process *process)
{
    if (process->state == PROCESS_WAITING && process->can_be_interrupted &&
        process->sleeping_in != nullptr && process->sleeping_in->remove(process))
        make_ready(process);
}

// The first code a new thread runs. schedule() left interrupts off.
static void thread_start()
{
    enable_interrupts();
    current_process->entry(current_process->argument);
    thread_exit(0);
}

Process *thread_create(const char *name, int priority,
                          void (*entry)(void *), void *argument)
{
    Process *thread = create_process(name, priority);
    uint32_t *stack;

    if (thread == nullptr || !kernel_stack_alloc(thread))
        panic("no memory for thread %s", name);

    thread->entry = entry;
    thread->argument = argument;

    // Build the stack switch_stacks() expects: four saved registers, then the
    // address it returns to.
    stack = (uint32_t *)thread->kernel_stack_top;
    *--stack = (uint32_t)thread_start;
    *--stack = 0;  // ebx
    *--stack = 0;  // esi
    *--stack = 0;  // edi
    *--stack = 0;  // ebp
    thread->saved_esp = (uint32_t)stack;

    add_new_process(thread);
    return thread;
}

void thread_exit(int status)
{
    uint32_t flags = turn_off_interrupts();

    current_process->exit_status = status;
    current_process->state = PROCESS_TERMINATED;
    exited.wake_all();
    schedule();

    restore_interrupts(flags);
    panic("a terminated thread was scheduled");
}

// Waits for a thread to finish, frees it and returns its exit status.
int thread_join(Process *thread)
{
    uint32_t flags = turn_off_interrupts();
    int status;

    while (thread->state != PROCESS_TERMINATED)
        exited.sleep();

    status = thread->exit_status;
    delete_process(thread);

    restore_interrupts(flags);
    return status;
}

void thread_sleep(uint32_t ticks)
{
    uint32_t flags = turn_off_interrupts();

    if (signal_pending()) {
        restore_interrupts(flags);
        return;
    }

    current_process->wake_tick = get_ticks() + ticks;
    current_process->state = PROCESS_WAITING;
    current_process->can_be_interrupted = true;
    current_process->sleeping_in = &sleeping;
    sleeping.add(current_process);
    schedule();

    restore_interrupts(flags);
}

// Wait queues

void WaitQueue::sleep(bool can_be_interrupted)
{
    uint32_t flags = turn_off_interrupts();

    // A signal that already arrived must not be slept through.
    if (can_be_interrupted && signal_pending()) {
        restore_interrupts(flags);
        return;
    }

    current_process->state = PROCESS_WAITING;
    current_process->can_be_interrupted = can_be_interrupted;
    current_process->sleeping_in = this;
    add(current_process);
    schedule();

    restore_interrupts(flags);
}

// Takes one process out of the middle of the line.
bool WaitQueue::remove(Process *process)
{
    Process *previous = nullptr;

    for (Process *item = head; item != nullptr; item = item->queue_next) {
        if (item == process) {
            if (previous != nullptr)
                previous->queue_next = item->queue_next;
            else
                head = item->queue_next;

            if (tail == item)
                tail = previous;

            item->queue_next = nullptr;
            return true;
        }
        previous = item;
    }
    return false;
}

void WaitQueue::wake_one()
{
    uint32_t flags = turn_off_interrupts();
    Process *process = remove_first();

    if (process != nullptr)
        make_ready(process);

    restore_interrupts(flags);
}

void WaitQueue::wake_all()
{
    uint32_t flags = turn_off_interrupts();
    Process *process;

    while ((process = remove_first()) != nullptr)
        make_ready(process);

    restore_interrupts(flags);
}

bool WaitQueue::is_empty() const
{
    return head == nullptr;
}

// The boot code carries on as the idle process, PID 0.
void scheduler_init()
{
    static Process idle;

    slab_cache_init(&pcb_cache, sizeof(Process));

    copy_string(idle.name, "idle", PROCESS_NAME_LENGTH);
    idle.pid = 0;
    idle.state = PROCESS_RUNNING;
    idle.priority = IDLE_PRIORITY;
    idle.base_priority = IDLE_PRIORITY;
    idle.page_dir = kernel_page_dir;

    idle_process = &idle;
    current_process = &idle;
    quantum_left = TIME_QUANTUM_TICKS;
}
