// Spinlock, Mutex, Semaphore and CondVar. See include/sync.h for what they are.

#include "console.h"
#include "hal.h"
#include "sync.h"

void Spinlock::lock()
{
    uint32_t flags = turn_off_interrupts();

    // One CPU and interrupts off means nobody else could ever unlock it.
    if (is_locked)
        panic("spinlock locked twice");

    is_locked = true;
    saved_flags = flags;
}

void Spinlock::unlock()
{
    uint32_t flags = saved_flags;

    is_locked = false;
    restore_interrupts(flags);
}

void Mutex::lock()
{
    uint32_t flags = turn_off_interrupts();

    while (is_locked)
        waiting_list.sleep();

    is_locked = true;
    owner = current_process;

    restore_interrupts(flags);
}

void Mutex::unlock()
{
    uint32_t flags = turn_off_interrupts();

    if (owner != current_process)
        panic("mutex unlocked by a process that does not own it");

    is_locked = false;
    owner = nullptr;
    waiting_list.wake_one();

    restore_interrupts(flags);
}

void Semaphore::set_count(int new_count)
{
    count = new_count;
}

void Semaphore::wait()
{
    uint32_t flags = turn_off_interrupts();

    while (count == 0)
        waiting_list.sleep();

    count--;
    restore_interrupts(flags);
}

void Semaphore::post()
{
    uint32_t flags = turn_off_interrupts();

    count++;
    waiting_list.wake_one();

    restore_interrupts(flags);
}

void CondVar::wait(Mutex &mutex)
{
    uint32_t flags = turn_off_interrupts();

    // Interrupts stay off between these two lines, so a signal can not be missed.
    mutex.unlock();
    waiting_list.sleep();
    restore_interrupts(flags);

    mutex.lock();
}

void CondVar::signal()
{
    waiting_list.wake_one();
}

void CondVar::broadcast()
{
    waiting_list.wake_all();
}
