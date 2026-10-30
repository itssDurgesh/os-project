/* Spinlock, mutex, semaphore and condition variable. See include/sync.h. */

#include <stddef.h>

#include "console.h"
#include "hal.h"
#include "sync.h"

void spinlock_init(struct spinlock *lock)
{
    lock->locked = false;
}

void spin_lock(struct spinlock *lock)
{
    uint32_t flags = irq_save();

    /* On one core with interrupts off, nobody else can release it. */
    if (lock->locked)
        panic("spinlock taken twice");

    lock->locked = true;
    lock->saved_flags = flags;
}

void spin_unlock(struct spinlock *lock)
{
    uint32_t flags = lock->saved_flags;

    lock->locked = false;
    irq_restore(flags);
}

void mutex_init(struct mutex *mutex)
{
    mutex->locked = false;
    mutex->owner = NULL;
    wait_queue_init(&mutex->waiters);
}

void mutex_lock(struct mutex *mutex)
{
    uint32_t flags = irq_save();

    while (mutex->locked)
        wait_queue_sleep(&mutex->waiters);

    mutex->locked = true;
    mutex->owner = current;

    irq_restore(flags);
}

void mutex_unlock(struct mutex *mutex)
{
    uint32_t flags = irq_save();

    if (mutex->owner != current)
        panic("mutex unlocked by a thread that doesn't own it");

    mutex->locked = false;
    mutex->owner = NULL;
    wait_queue_wake_one(&mutex->waiters);

    irq_restore(flags);
}

void semaphore_init(struct semaphore *semaphore, int count)
{
    semaphore->count = count;
    wait_queue_init(&semaphore->waiters);
}

void semaphore_wait(struct semaphore *semaphore)
{
    uint32_t flags = irq_save();

    while (semaphore->count == 0)
        wait_queue_sleep(&semaphore->waiters);

    semaphore->count--;
    irq_restore(flags);
}

void semaphore_post(struct semaphore *semaphore)
{
    uint32_t flags = irq_save();

    semaphore->count++;
    wait_queue_wake_one(&semaphore->waiters);

    irq_restore(flags);
}

void condvar_init(struct condvar *condvar)
{
    wait_queue_init(&condvar->waiters);
}

void condvar_wait(struct condvar *condvar, struct mutex *mutex)
{
    uint32_t flags = irq_save();

    /* Interrupts stay off between these two steps, so a signal can't be lost. */
    mutex_unlock(mutex);
    wait_queue_sleep(&condvar->waiters);
    irq_restore(flags);

    mutex_lock(mutex);
}

void condvar_signal(struct condvar *condvar)
{
    wait_queue_wake_one(&condvar->waiters);
}

void condvar_broadcast(struct condvar *condvar)
{
    wait_queue_wake_all(&condvar->waiters);
}
