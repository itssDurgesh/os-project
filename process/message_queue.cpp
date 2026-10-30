// Message queues: a kernel-managed line of typed messages between processes.

#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "syscall.h"

#define MAX_QUEUES 8
#define MESSAGES_PER_QUEUE 16

struct Message {
    int type;
    uint32_t size;
    char data[MAX_MESSAGE_SIZE];
};

class MessageQueue {
public:
    bool in_use = false;
    int key = 0;
    int count = 0;
    Message messages[MESSAGES_PER_QUEUE];
    WaitQueue can_send;
    WaitQueue can_receive;

    int find_message(int wanted_type);
    void remove_message(int index);
};

static MessageQueue all_queues[MAX_QUEUES];

// Returns the position of the oldest message of this type. Type 0 means any.
int MessageQueue::find_message(int wanted_type)
{
    for (int i = 0; i < count; i++) {
        if (wanted_type == 0 || messages[i].type == wanted_type)
            return i;
    }
    return -1;
}

void MessageQueue::remove_message(int index)
{
    for (int i = index; i < count - 1; i++)
        messages[i] = messages[i + 1];
    count--;
}

static MessageQueue *get_queue(int id)
{
    if (id < 0 || id >= MAX_QUEUES || !all_queues[id].in_use)
        return nullptr;
    return &all_queues[id];
}

int sys_msgget(Registers *r)
{
    int key = (int)r->ebx;

    for (int i = 0; i < MAX_QUEUES; i++) {
        if (all_queues[i].in_use && all_queues[i].key == key)
            return i;
    }

    for (int i = 0; i < MAX_QUEUES; i++) {
        if (!all_queues[i].in_use) {
            all_queues[i].in_use = true;
            all_queues[i].key = key;
            all_queues[i].count = 0;
            return i;
        }
    }
    return -ENOMEM;
}

int sys_msgsnd(Registers *r)
{
    MessageQueue *queue = get_queue((int)r->ebx);
    int type = (int)r->ecx;
    const void *data = (const void *)r->edx;
    uint32_t size = r->esi;

    if (queue == nullptr || type <= 0 || size > MAX_MESSAGE_SIZE)
        return -EINVAL;
    if (!user_range_valid(data, size, false))
        return -EFAULT;

    while (queue->count == MESSAGES_PER_QUEUE) {
        queue->can_send.sleep(true);
        if (signal_pending())
            return -EINTR;
    }

    Message *message = &queue->messages[queue->count];

    message->type = type;
    message->size = size;
    memcpy(message->data, data, size);
    queue->count++;

    queue->can_receive.wake_all();
    return 0;
}

// Returns the size of the message it received.
int sys_msgrcv(Registers *r)
{
    MessageQueue *queue = get_queue((int)r->ebx);
    int wanted_type = (int)r->ecx;
    void *data = (void *)r->edx;
    uint32_t max_size = r->esi;
    int index;

    if (queue == nullptr)
        return -EINVAL;
    if (!user_range_valid(data, max_size, true))
        return -EFAULT;

    while ((index = queue->find_message(wanted_type)) < 0) {
        queue->can_receive.sleep(true);
        if (signal_pending())
            return -EINTR;
    }

    Message *message = &queue->messages[index];
    uint32_t size = message->size;

    if (size > max_size)
        return -EINVAL;

    memcpy(data, message->data, size);
    queue->remove_message(index);
    queue->can_send.wake_all();
    return (int)size;
}
