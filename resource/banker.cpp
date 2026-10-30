// Banker's Algorithm: deadlock avoidance. Each process first says the most it
// could ever need. A request is only granted if, afterwards, there is still some
// order in which every process could finish. That is called a safe state.
//
// It covers three limited resources: pipes, message queues and shared memory
// segments. Kernel locks don't use it. They are taken in a fixed order instead.

#include "errno.h"
#include "memory.h"
#include "process.h"
#include "syscall.h"

static int total_resources[RESOURCE_TYPES] = { 8, 4, 4 };
static int available[RESOURCE_TYPES] = { 8, 4, 4 };
static WaitQueue waiting_for_resources;

// The test from the textbook: keep finding a process that could finish with what
// is free, pretend it finished and gave its resources back, and repeat.
static bool system_is_safe()
{
    int free_now[RESOURCE_TYPES];
    bool found_one = true;

    for (int type = 0; type < RESOURCE_TYPES; type++)
        free_now[type] = available[type];

    for (Process *p = process_list(); p != nullptr; p = p->list_next)
        p->banker_finished = !p->banker_declared;

    while (found_one) {
        found_one = false;

        for (Process *p = process_list(); p != nullptr; p = p->list_next) {
            bool can_finish = !p->banker_finished;

            for (int type = 0; type < RESOURCE_TYPES; type++) {
                int still_needed = p->banker_maximum[type] - p->banker_allocated[type];

                if (still_needed > free_now[type])
                    can_finish = false;
            }

            if (can_finish) {
                for (int type = 0; type < RESOURCE_TYPES; type++)
                    free_now[type] += p->banker_allocated[type];

                p->banker_finished = true;
                found_one = true;
            }
        }
    }

    for (Process *p = process_list(); p != nullptr; p = p->list_next) {
        if (!p->banker_finished)
            return false;
    }
    return true;
}

// Pretends to give out the resources, and takes them back if that isn't safe.
static bool try_to_grant(int type, int amount)
{
    if (amount > available[type])
        return false;

    available[type] -= amount;
    current_process->banker_allocated[type] += amount;

    if (system_is_safe())
        return true;

    available[type] += amount;
    current_process->banker_allocated[type] -= amount;
    return false;
}

int sys_res_declare(Registers *r)
{
    int *user_maximum = (int *)r->ebx;

    if (!user_range_valid(user_maximum, RESOURCE_TYPES * sizeof(int), false))
        return -EFAULT;

    for (int type = 0; type < RESOURCE_TYPES; type++) {
        if (user_maximum[type] < 0 || user_maximum[type] > total_resources[type])
            return -EINVAL;
    }

    for (int type = 0; type < RESOURCE_TYPES; type++)
        current_process->banker_maximum[type] = user_maximum[type];

    current_process->banker_declared = true;
    return 0;
}

// Sleeps until the request can be granted safely.
int sys_res_request(Registers *r)
{
    int type = (int)r->ebx;
    int amount = (int)r->ecx;

    if (!current_process->banker_declared || type < 0 || type >= RESOURCE_TYPES ||
        amount < 0)
        return -EINVAL;

    if (current_process->banker_allocated[type] + amount >
        current_process->banker_maximum[type])
        return -EINVAL;

    while (!try_to_grant(type, amount)) {
        waiting_for_resources.sleep(true);
        if (signal_pending())
            return -EINTR;
    }
    return 0;
}

int sys_res_release(Registers *r)
{
    int type = (int)r->ebx;
    int amount = (int)r->ecx;

    if (type < 0 || type >= RESOURCE_TYPES || amount < 0 ||
        amount > current_process->banker_allocated[type])
        return -EINVAL;

    current_process->banker_allocated[type] -= amount;
    available[type] += amount;
    waiting_for_resources.wake_all();
    return 0;
}

void banker_process_exit(Process *process)
{
    for (int type = 0; type < RESOURCE_TYPES; type++) {
        available[type] += process->banker_allocated[type];
        process->banker_allocated[type] = 0;
    }

    process->banker_declared = false;
    waiting_for_resources.wake_all();
}
