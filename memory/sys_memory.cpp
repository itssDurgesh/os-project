// Memory system calls. brk() and sbrk() move the top of a program's heap.

#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "syscall.h"

#define SHARED_MEMORY_START 0x40000000u  // the heap has to stay below this
#define FIRST_MMAP_ADDRESS 0x50000000u
#define LAST_MMAP_ADDRESS 0xB0000000u

static int set_heap_end(uint32_t new_end)
{
    MemoryArea *heap = &current_process->areas[current_process->heap_area];
    uint32_t old_top = heap->end;
    uint32_t new_top = round_up_to_page(new_end);

    if (new_end < heap->start || new_end >= SHARED_MEMORY_START)
        return -ENOMEM;

    // Growing needs no work here: new pages arrive when they are first used.
    for (uint32_t virt = new_top; virt < old_top; virt += PAGE_SIZE)
        unmap_user_page(current_process->page_dir, virt);

    heap->end = new_top;
    current_process->heap_end = new_end;
    return 0;
}

int sys_brk(Registers *r)
{
    return set_heap_end(r->ebx);
}

// Returns the old end of the heap, which is the start of the new space.
int sys_sbrk(Registers *r)
{
    uint32_t old_end = current_process->heap_end;
    int status = set_heap_end(old_end + (int)r->ebx);

    return (status != 0) ? status : (int)old_end;
}

// Gives the program a new zero-filled area of memory.
int sys_mmap(Registers *r)
{
    uint32_t size = round_up_to_page(r->ebx);
    uint32_t address = current_process->next_mmap_address;

    if (address == 0)
        address = FIRST_MMAP_ADDRESS;

    if (size == 0 || address + size > LAST_MMAP_ADDRESS)
        return -ENOMEM;

    int area = add_memory_area(current_process->areas, address, address + size,
                               PTE_USER | PTE_WRITABLE, nullptr, 0, 0);

    if (area < 0)
        return area;

    current_process->next_mmap_address = address + size;
    return (int)address;
}

int sys_munmap(Registers *r)
{
    uint32_t address = r->ebx;

    for (int i = 0; i < MAX_MEMORY_AREAS; i++) {
        MemoryArea *area = &current_process->areas[i];

        if (area->in_use && area->start == address && area->file == nullptr &&
            i != current_process->heap_area) {
            for (uint32_t virt = area->start; virt < area->end; virt += PAGE_SIZE)
                unmap_user_page(current_process->page_dir, virt);
            area->in_use = false;
            return 0;
        }
    }
    return -EINVAL;
}
