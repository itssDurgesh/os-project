/* Memory system calls. brk() and sbrk() move the top of a program's heap. */

#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "syscall.h"

#define USER_STACK_RESERVE (1024 * 1024)    /* keep the heap below the stack */

static int set_heap_end(uint32_t new_end)
{
    uint32_t old_top = PAGE_ALIGN_UP(current->heap_end);
    uint32_t new_top = PAGE_ALIGN_UP(new_end);

    if (new_end >= KERNEL_VMA - USER_STACK_RESERVE)
        return -ENOMEM;

    for (uint32_t virt = old_top; virt < new_top; virt += PAGE_SIZE) {
        uint32_t frame = alloc_frame();

        if (frame == 0)
            return -ENOMEM;

        memset(P2V(frame), 0, PAGE_SIZE);
        if (paging_map(current->page_dir, virt, frame,
                       PTE_USER | PTE_WRITABLE) != 0) {
            free_frame(frame);
            return -ENOMEM;
        }
    }

    for (uint32_t virt = new_top; virt < old_top; virt += PAGE_SIZE) {
        uint32_t frame = paging_unmap(current->page_dir, virt);

        if (frame != 0)
            free_frame(frame);
    }

    current->heap_end = new_end;
    return 0;
}

int sys_brk(struct regs *r)
{
    return set_heap_end(r->ebx);
}

/* Returns the old end of the heap, which is the start of the new space. */
int sys_sbrk(struct regs *r)
{
    uint32_t old_end = current->heap_end;
    int status = set_heap_end(old_end + (int)r->ebx);

    return (status != 0) ? status : (int)old_end;
}
