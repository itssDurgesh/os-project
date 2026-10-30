// Semaphores for user programs. The kernel's own Semaphore class can not be woken
// by a signal, so this one has its own wait loop.

#include "errno.h"
#include "process.h"
#include "syscall.h"

#define MAX_USER_SEMAPHORES 16

class UserSemaphore {
public:
    bool in_use = false;
    int key = 0;
    int count = 0;
    int open_count = 0;
    WaitQueue waiting_list;
};

static UserSemaphore all_semaphores[MAX_USER_SEMAPHORES];

static UserSemaphore *get_semaphore(int id)
{
    if (id < 0 || id >= MAX_USER_SEMAPHORES || !all_semaphores[id].in_use)
        return nullptr;
    return &all_semaphores[id];
}

int sys_sem_open(Registers *r)
{
    int key = (int)r->ebx;
    int starting_value = (int)r->ecx;

    if (starting_value < 0)
        return -EINVAL;

    for (int i = 0; i < MAX_USER_SEMAPHORES; i++) {
        if (all_semaphores[i].in_use && all_semaphores[i].key == key) {
            all_semaphores[i].open_count++;
            return i;
        }
    }

    for (int i = 0; i < MAX_USER_SEMAPHORES; i++) {
        if (!all_semaphores[i].in_use) {
            all_semaphores[i].in_use = true;
            all_semaphores[i].key = key;
            all_semaphores[i].count = starting_value;
            all_semaphores[i].open_count = 1;
            return i;
        }
    }
    return -ENOMEM;
}

int sys_sem_wait(Registers *r)
{
    UserSemaphore *semaphore = get_semaphore((int)r->ebx);

    if (semaphore == nullptr)
        return -EINVAL;

    while (semaphore->count == 0) {
        semaphore->waiting_list.sleep(true);
        if (signal_pending())
            return -EINTR;
    }

    semaphore->count--;
    return 0;
}

int sys_sem_post(Registers *r)
{
    UserSemaphore *semaphore = get_semaphore((int)r->ebx);

    if (semaphore == nullptr)
        return -EINVAL;

    semaphore->count++;
    semaphore->waiting_list.wake_one();
    return 0;
}

int sys_sem_close(Registers *r)
{
    UserSemaphore *semaphore = get_semaphore((int)r->ebx);

    if (semaphore == nullptr)
        return -EINVAL;

    semaphore->open_count--;
    if (semaphore->open_count == 0)
        semaphore->in_use = false;
    return 0;
}
