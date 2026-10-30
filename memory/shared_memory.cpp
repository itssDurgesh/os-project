// Shared memory: two or more processes map the same physical frames, so a write
// by one is seen by the others straight away. They use semaphores to take turns.

#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "syscall.h"

#define MAX_SEGMENTS 8
#define MAX_SEGMENT_PAGES 16
#define SHARED_MEMORY_START 0x40000000u
#define SEGMENT_SPACE (MAX_SEGMENT_PAGES * PAGE_SIZE)

struct SharedSegment {
    bool in_use;
    int key;
    uint32_t page_count;
    int attach_count;
    uint32_t frames[MAX_SEGMENT_PAGES];
};

static SharedSegment all_segments[MAX_SEGMENTS];

static uint32_t slot_address(int slot)
{
    return SHARED_MEMORY_START + slot * SEGMENT_SPACE;
}

static void delete_segment(SharedSegment *segment)
{
    for (uint32_t i = 0; i < segment->page_count; i++)
        free_frame(segment->frames[i]);

    segment->in_use = false;
}

int sys_shmget(Registers *r)
{
    int key = (int)r->ebx;
    uint32_t page_count = round_up_to_page(r->ecx) / PAGE_SIZE;

    if (page_count == 0 || page_count > MAX_SEGMENT_PAGES)
        return -EINVAL;

    for (int i = 0; i < MAX_SEGMENTS; i++) {
        if (all_segments[i].in_use && all_segments[i].key == key)
            return i;
    }

    for (int i = 0; i < MAX_SEGMENTS; i++) {
        SharedSegment *segment = &all_segments[i];

        if (segment->in_use)
            continue;

        for (uint32_t page = 0; page < page_count; page++) {
            segment->frames[page] = alloc_frame();
            if (segment->frames[page] == 0) {
                segment->page_count = page;
                delete_segment(segment);
                return -ENOMEM;
            }
            memset(phys_to_virt(segment->frames[page]), 0, PAGE_SIZE);
        }

        segment->in_use = true;
        segment->key = key;
        segment->page_count = page_count;
        segment->attach_count = 0;
        return i;
    }
    return -ENOMEM;
}

// Maps the segment into this process and returns its address.
int sys_shmat(Registers *r)
{
    int id = (int)r->ebx;
    int slot = -1;

    if (id < 0 || id >= MAX_SEGMENTS || !all_segments[id].in_use)
        return -EINVAL;

    for (int i = 0; i < MAX_ATTACHED_SEGMENTS; i++) {
        if (current_process->attached_segments[i] == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -ENOMEM;

    SharedSegment *segment = &all_segments[id];

    for (uint32_t page = 0; page < segment->page_count; page++) {
        uint32_t address = slot_address(slot) + page * PAGE_SIZE;

        add_frame_user(segment->frames[page]);
        paging_map(current_process->page_dir, address, segment->frames[page],
                   PTE_USER | PTE_WRITABLE | PTE_SHARED);
    }

    current_process->attached_segments[slot] = id + 1;  // 0 means an empty slot
    segment->attach_count++;
    return (int)slot_address(slot);
}

static void detach_slot(Process *process, int slot)
{
    SharedSegment *segment = &all_segments[process->attached_segments[slot] - 1];

    for (uint32_t page = 0; page < segment->page_count; page++) {
        uint32_t frame = paging_unmap(process->page_dir,
                                      slot_address(slot) + page * PAGE_SIZE);

        if (frame != 0)
            free_frame(frame);
    }

    process->attached_segments[slot] = 0;
    segment->attach_count--;
    if (segment->attach_count == 0)
        delete_segment(segment);
}

int sys_shmdt(Registers *r)
{
    for (int slot = 0; slot < MAX_ATTACHED_SEGMENTS; slot++) {
        if (current_process->attached_segments[slot] != 0 &&
            slot_address(slot) == r->ebx) {
            detach_slot(current_process, slot);
            return 0;
        }
    }
    return -EINVAL;
}

void shared_memory_detach_all(Process *process)
{
    for (int slot = 0; slot < MAX_ATTACHED_SEGMENTS; slot++) {
        if (process->attached_segments[slot] != 0)
            detach_slot(process, slot);
    }
}

// After fork the child can use the same segments, so they have one more user.
void shared_memory_copy_to_child(Process *parent, Process *child)
{
    for (int slot = 0; slot < MAX_ATTACHED_SEGMENTS; slot++) {
        int value = parent->attached_segments[slot];

        child->attached_segments[slot] = value;
        if (value != 0)
            all_segments[value - 1].attach_count++;
    }
}
