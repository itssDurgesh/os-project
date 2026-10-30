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

extern void switch_context(uint32_t *saved_esp, uint32_t next_esp);

struct pcb *current;

static struct wait_queue ready_queues[NUM_PRIORITIES];
static struct wait_queue sleeping;      /* threads inside thread_sleep() */
static struct wait_queue exited;        /* threads waiting in thread_join() */
static struct slab_cache pcb_cache;
static struct pcb *idle_process;
static struct pcb *all_processes;
static int next_pid = 1;
static int quantum_left;
static volatile bool need_resched;

/* Linked queues */

static void queue_push(struct wait_queue *queue, struct pcb *process)
{
    process->queue_next = NULL;
    if (queue->tail != NULL)
        queue->tail->queue_next = process;
    else
        queue->head = process;
    queue->tail = process;
}

static struct pcb *queue_pop(struct wait_queue *queue)
{
    struct pcb *process = queue->head;

    if (process != NULL) {
        queue->head = process->queue_next;
        if (queue->head == NULL)
            queue->tail = NULL;
        process->queue_next = NULL;
    }
    return process;
}

static void make_ready(struct pcb *process)
{
    process->state = PROCESS_READY;
    process->ready_since = pit_ticks();
    queue_push(&ready_queues[process->priority], process);

    if (process->priority < current->priority)
        need_resched = true;
}

static struct pcb *pick_next(void)
{
    for (int priority = 0; priority < NUM_PRIORITIES; priority++) {
        struct pcb *process = queue_pop(&ready_queues[priority]);

        if (process != NULL)
            return process;
    }
    return idle_process;
}

/* Scheduling */

void schedule(void)
{
    uint32_t flags = irq_save();
    struct pcb *previous = current;
    struct pcb *next;

    need_resched = false;

    if (previous->state == PROCESS_RUNNING && previous != idle_process) {
        previous->priority = previous->base_priority;
        make_ready(previous);
    }

    next = pick_next();
    next->state = PROCESS_RUNNING;
    quantum_left = TIME_QUANTUM_TICKS;

    if (next != previous) {
        current = next;
        tss_set_kernel_stack(next->kernel_stack_top);
        if (next->page_dir != previous->page_dir)
            paging_switch(next->page_dir);
        switch_context(&previous->saved_esp, next->saved_esp);
    }

    irq_restore(flags);
}

/* Moves any process that has waited too long up one priority level. */
static void age_ready_queues(uint32_t now)
{
    for (int priority = 1; priority < NUM_PRIORITIES; priority++) {
        struct wait_queue *queue = &ready_queues[priority];
        struct wait_queue kept = { NULL, NULL };
        struct pcb *process;

        while ((process = queue_pop(queue)) != NULL) {
            if (now - process->ready_since >= AGING_LIMIT_TICKS) {
                process->priority = priority - 1;
                make_ready(process);
            } else {
                queue_push(&kept, process);
            }
        }
        *queue = kept;
    }
}

static void wake_sleepers(uint32_t now)
{
    struct wait_queue still_sleeping = { NULL, NULL };
    struct pcb *process;

    while ((process = queue_pop(&sleeping)) != NULL) {
        if (now - process->wake_tick < 0x80000000u)
            make_ready(process);
        else
            queue_push(&still_sleeping, process);
    }
    sleeping = still_sleeping;
}

/* Called from the timer interrupt, every 10 ms. */
void sched_tick(void)
{
    uint32_t now = pit_ticks();

    if (current == NULL)
        return;

    current->cpu_ticks++;
    wake_sleepers(now);

    if (now % AGING_CHECK_TICKS == 0)
        age_ready_queues(now);

    if (current == idle_process) {
        need_resched = true;
    } else if (--quantum_left <= 0) {
        need_resched = true;
    }
}

void sched_check_preempt(void)
{
    if (need_resched)
        schedule();
}

void sched_checkpoint(void)
{
    if (need_resched)
        schedule();
}

void sched_idle_loop(void)
{
    for (;;) {
        if (need_resched)
            schedule();
        cpu_sti_halt();
    }
}

/* Kernel threads */

struct pcb *pcb_create(const char *name, int priority)
{
    struct pcb *process = slab_alloc(&pcb_cache);

    if (process == NULL)
        return NULL;

    memset(process, 0, sizeof(*process));
    process->pid = next_pid++;
    process->parent_pid = (current != NULL) ? current->pid : 0;
    process->state = PROCESS_NEW;
    process->priority = priority;
    process->base_priority = priority;
    process->page_dir = kernel_page_dir;
    strlcpy(process->name, name, PROCESS_NAME_LENGTH);

    process->list_next = all_processes;
    all_processes = process;
    return process;
}

/* Frees a finished process. Nothing may run on its stack any more. */
void pcb_free(struct pcb *process)
{
    struct pcb **link;

    for (link = &all_processes; *link != NULL; link = &(*link)->list_next) {
        if (*link == process) {
            *link = process->list_next;
            break;
        }
    }

    free_frames(V2P(process->kernel_stack));
    slab_free(process);
}

void kernel_stack_alloc(struct pcb *process)
{
    uint32_t frames = alloc_frames(KERNEL_STACK_PAGES);

    if (frames == 0)
        panic("no memory for a kernel stack");

    process->kernel_stack = P2V(frames);
    process->kernel_stack_top = (uint32_t)process->kernel_stack + KERNEL_STACK_SIZE;
}

void sched_add_new(struct pcb *process)
{
    uint32_t flags = irq_save();

    make_ready(process);
    irq_restore(flags);
}

struct pcb *process_list(void)
{
    return all_processes;
}

/* The first code a new thread runs. schedule() left interrupts off. */
static void thread_start(void)
{
    cpu_sti();
    current->entry(current->argument);
    thread_exit(0);
}

struct pcb *thread_create(const char *name, int priority,
                          void (*entry)(void *), void *argument)
{
    struct pcb *thread = pcb_create(name, priority);
    uint32_t *stack;

    if (thread == NULL)
        panic("no memory for thread %s", name);

    kernel_stack_alloc(thread);
    thread->entry = entry;
    thread->argument = argument;

    /* Build the stack switch_context() expects: four saved registers, then the
     * address it returns to. */
    stack = (uint32_t *)thread->kernel_stack_top;
    *--stack = (uint32_t)thread_start;
    *--stack = 0;   /* ebx */
    *--stack = 0;   /* esi */
    *--stack = 0;   /* edi */
    *--stack = 0;   /* ebp */
    thread->saved_esp = (uint32_t)stack;

    sched_add_new(thread);
    return thread;
}

void thread_exit(int status)
{
    uint32_t flags = irq_save();

    current->exit_status = status;
    current->state = PROCESS_TERMINATED;
    wait_queue_wake_all(&exited);
    schedule();

    irq_restore(flags);
    panic("a terminated thread was scheduled");
}

/* Waits for a thread to finish, frees it and returns its exit status. */
int thread_join(struct pcb *thread)
{
    uint32_t flags = irq_save();
    int status;

    while (thread->state != PROCESS_TERMINATED)
        wait_queue_sleep(&exited);

    status = thread->exit_status;
    pcb_free(thread);

    irq_restore(flags);
    return status;
}

void thread_sleep(uint32_t ticks)
{
    uint32_t flags = irq_save();

    current->wake_tick = pit_ticks() + ticks;
    current->state = PROCESS_WAITING;
    queue_push(&sleeping, current);
    schedule();

    irq_restore(flags);
}

/* Wait queues */

void wait_queue_init(struct wait_queue *queue)
{
    queue->head = NULL;
    queue->tail = NULL;
}

void wait_queue_sleep(struct wait_queue *queue)
{
    uint32_t flags = irq_save();

    current->state = PROCESS_WAITING;
    queue_push(queue, current);
    schedule();

    irq_restore(flags);
}

void wait_queue_wake_one(struct wait_queue *queue)
{
    uint32_t flags = irq_save();
    struct pcb *process = queue_pop(queue);

    if (process != NULL)
        make_ready(process);

    irq_restore(flags);
}

void wait_queue_wake_all(struct wait_queue *queue)
{
    uint32_t flags = irq_save();
    struct pcb *process;

    while ((process = queue_pop(queue)) != NULL)
        make_ready(process);

    irq_restore(flags);
}

bool wait_queue_empty(const struct wait_queue *queue)
{
    return queue->head == NULL;
}

/* The boot code carries on as the idle process, PID 0. */
void sched_init(void)
{
    static struct pcb idle;

    slab_cache_init(&pcb_cache, sizeof(struct pcb));

    strlcpy(idle.name, "idle", PROCESS_NAME_LENGTH);
    idle.pid = 0;
    idle.state = PROCESS_RUNNING;
    idle.priority = IDLE_PRIORITY;
    idle.base_priority = IDLE_PRIORITY;
    idle.page_dir = kernel_page_dir;

    idle_process = &idle;
    current = &idle;
    quantum_left = TIME_QUANTUM_TICKS;
}
