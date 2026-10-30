/* Round-Robin scheduler with four priority levels and aging.
 *
 * Ready queues are also changed by the timer interrupt and by drivers waking a
 * process, so every change to them runs with interrupts off. */

#include <stddef.h>

#include "console.h"
#include "hal.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"

#define KERNEL_STACK_PAGES 2
#define KERNEL_STACK_SIZE  (KERNEL_STACK_PAGES * PAGE_SIZE)
#define AGING_CHECK_TICKS  10

#define IDLE_PRIORITY NUM_PRIORITIES    /* below every ready queue */

extern "C" void switch_stacks(uint32_t *saved_esp, uint32_t next_esp);

struct Process *current_process;

static struct WaitQueue ready_queues[NUM_PRIORITIES];
static struct WaitQueue sleeping;      /* threads inside thread_sleep() */
static struct WaitQueue exited;        /* threads waiting in thread_join() */
static SlabCache pcb_cache;
static struct Process *idle_process;
static struct Process *all_processes;
static int next_pid = 1;
static int quantum_left;
static volatile bool need_resched;

/* Linked queues */

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

static void make_ready(struct Process *process)
{
    process->state = PROCESS_READY;
    process->ready_since = get_ticks();
    ready_queues[process->priority].add(process);

    if (process->priority < current_process->priority)
        need_resched = true;
}

static struct Process *pick_next(void)
{
    for (int priority = 0; priority < NUM_PRIORITIES; priority++) {
        struct Process *process = ready_queues[priority].remove_first();

        if (process != NULL)
            return process;
    }
    return idle_process;
}

/* Scheduling */

void schedule(void)
{
    uint32_t flags = turn_off_interrupts();
    struct Process *previous = current_process;
    struct Process *next;

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

/* Moves any process that has waited too long up one priority level. */
static void age_ready_queues(uint32_t now)
{
    for (int priority = 1; priority < NUM_PRIORITIES; priority++) {
        struct WaitQueue *queue = &ready_queues[priority];
        WaitQueue kept;
        struct Process *process;

        while ((process = queue->remove_first()) != NULL) {
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
    struct Process *process;

    while ((process = sleeping.remove_first()) != NULL) {
        if (now - process->wake_tick < 0x80000000u)
            make_ready(process);
        else
            still_sleeping.add(process);
    }
    sleeping = still_sleeping;
}

/* Called from the timer interrupt, every 10 ms. */
void scheduler_tick(void)
{
    uint32_t now = get_ticks();

    if (current_process == NULL)
        return;

    current_process->cpu_ticks++;
    wake_sleepers(now);

    if (now % AGING_CHECK_TICKS == 0)
        age_ready_queues(now);

    if (current_process == idle_process) {
        need_resched = true;
    } else if (--quantum_left <= 0) {
        need_resched = true;
    }
}

void switch_process_if_needed(void)
{
    if (need_resched)
        schedule();
}

void give_up_cpu_if_needed(void)
{
    if (need_resched)
        schedule();
}

void idle_loop(void)
{
    for (;;) {
        if (need_resched)
            schedule();
        enable_interrupts_and_wait();
    }
}

/* Kernel threads */

struct Process *create_process(const char *name, int priority)
{
    Process *process = (Process *)slab_alloc(&pcb_cache);

    if (process == NULL)
        return NULL;

    memset(process, 0, sizeof(*process));
    process->pid = next_pid++;
    process->parent_pid = (current_process != NULL) ? current_process->pid : 0;
    process->state = PROCESS_NEW;
    process->priority = priority;
    process->base_priority = priority;
    process->page_dir = kernel_page_dir;
    copy_string(process->name, name, PROCESS_NAME_LENGTH);

    process->list_next = all_processes;
    all_processes = process;
    return process;
}

/* Frees a finished process. Nothing may run on its stack any more. */
void delete_process(struct Process *process)
{
    struct Process **link;

    for (link = &all_processes; *link != NULL; link = &(*link)->list_next) {
        if (*link == process) {
            *link = process->list_next;
            break;
        }
    }

    free_frames(virt_to_phys(process->kernel_stack));
    slab_free(process);
}

void kernel_stack_alloc(struct Process *process)
{
    uint32_t frames = alloc_frames(KERNEL_STACK_PAGES);

    if (frames == 0)
        panic("no memory for a kernel stack");

    process->kernel_stack = phys_to_virt(frames);
    process->kernel_stack_top = (uint32_t)process->kernel_stack + KERNEL_STACK_SIZE;
}

void add_new_process(struct Process *process)
{
    uint32_t flags = turn_off_interrupts();

    make_ready(process);
    restore_interrupts(flags);
}

struct Process *process_list(void)
{
    return all_processes;
}

/* The first code a new thread runs. schedule() left interrupts off. */
static void thread_start(void)
{
    enable_interrupts();
    current_process->entry(current_process->argument);
    thread_exit(0);
}

struct Process *thread_create(const char *name, int priority,
                          void (*entry)(void *), void *argument)
{
    struct Process *thread = create_process(name, priority);
    uint32_t *stack;

    if (thread == NULL)
        panic("no memory for thread %s", name);

    kernel_stack_alloc(thread);
    thread->entry = entry;
    thread->argument = argument;

    /* Build the stack switch_stacks() expects: four saved registers, then the
     * address it returns to. */
    stack = (uint32_t *)thread->kernel_stack_top;
    *--stack = (uint32_t)thread_start;
    *--stack = 0;   /* ebx */
    *--stack = 0;   /* esi */
    *--stack = 0;   /* edi */
    *--stack = 0;   /* ebp */
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

/* Waits for a thread to finish, frees it and returns its exit status. */
int thread_join(struct Process *thread)
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

    current_process->wake_tick = get_ticks() + ticks;
    current_process->state = PROCESS_WAITING;
    sleeping.add(current_process);
    schedule();

    restore_interrupts(flags);
}

/* Wait queues */

void WaitQueue::sleep()
{
    uint32_t flags = turn_off_interrupts();

    current_process->state = PROCESS_WAITING;
    add(current_process);
    schedule();

    restore_interrupts(flags);
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

/* The boot code carries on as the idle process, PID 0. */
void scheduler_init(void)
{
    static struct Process idle;

    slab_cache_init(&pcb_cache, sizeof(struct Process));

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
