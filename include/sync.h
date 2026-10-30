// Locks for the kernel (owner: Resource Management).
//
// Our OS has one CPU and the kernel is not preempted, so the only thing that can
// interrupt kernel code is an interrupt handler. That is why a Spinlock just
// turns interrupts off. The other locks put the process to sleep, so they can
// only be used by a process, never inside an interrupt handler.

#ifndef SYNC_H
#define SYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "process.h"

class Spinlock {
public:
    void lock();
    void unlock();

private:
    bool is_locked = false;
    uint32_t saved_flags = 0;
};

class Mutex {
public:
    void lock();
    void unlock();

private:
    bool is_locked = false;
    Process *owner = nullptr;
    WaitQueue waiting_list;
};

class Semaphore {
public:
    void set_count(int new_count);
    void wait();
    void post();

private:
    int count = 0;
    WaitQueue waiting_list;
};

class CondVar {
public:
    void wait(Mutex &mutex);
    void signal();
    void broadcast();

private:
    WaitQueue waiting_list;
};

#endif
