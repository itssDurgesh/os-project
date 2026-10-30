/* Synchronization primitives (owner: Resource Management).
 *
 * The OS runs on one core and the kernel isn't preempted, so only an interrupt
 * handler can interrupt kernel code. A spinlock therefore just turns interrupts
 * off. Mutexes, semaphores and condition variables put the caller to sleep, so
 * they can only be used from a thread, never from an interrupt handler. */

#ifndef SYNC_H
#define SYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "process.h"

struct spinlock {
    bool locked;
    uint32_t saved_flags;
};

struct mutex {
    bool locked;
    struct pcb *owner;
    struct wait_queue waiters;
};

struct semaphore {
    int count;
    struct wait_queue waiters;
};

struct condvar {
    struct wait_queue waiters;
};

void spinlock_init(struct spinlock *lock);
void spin_lock(struct spinlock *lock);
void spin_unlock(struct spinlock *lock);

void mutex_init(struct mutex *mutex);
void mutex_lock(struct mutex *mutex);
void mutex_unlock(struct mutex *mutex);

void semaphore_init(struct semaphore *semaphore, int count);
void semaphore_wait(struct semaphore *semaphore);
void semaphore_post(struct semaphore *semaphore);

void condvar_init(struct condvar *condvar);
void condvar_wait(struct condvar *condvar, struct mutex *mutex);
void condvar_signal(struct condvar *condvar);
void condvar_broadcast(struct condvar *condvar);

#endif
