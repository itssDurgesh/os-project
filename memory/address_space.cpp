// Per-process address spaces, and safe access to user memory from the kernel.

#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "storage.h"

#define ENTRIES_PER_TABLE 1024
#define KERNEL_PD_START   (KERNEL_VMA >> 22)  // entry 768
#define FLAGS_MASK        (PAGE_SIZE - 1)

// A new address space holds only the kernel's shared mappings.
uint32_t *make_page_directory()
{
    uint32_t frame = alloc_frame();
    uint32_t *page_dir;

    if (frame == 0)
        return nullptr;

    page_dir = (uint32_t *)phys_to_virt(frame);
    memset(page_dir, 0, PAGE_SIZE);
    memcpy(&page_dir[KERNEL_PD_START], &kernel_page_dir[KERNEL_PD_START],
           (ENTRIES_PER_TABLE - KERNEL_PD_START) * sizeof(uint32_t));
    return page_dir;
}

// Gives a child its own copy of every user page.
uint32_t *copy_page_directory(const uint32_t *parent)
{
    uint32_t *child = make_page_directory();

    if (child == nullptr)
        return nullptr;

    for (uint32_t directory = 0; directory < KERNEL_PD_START; directory++) {
        const uint32_t *table;

        if ((parent[directory] & PTE_PRESENT) == 0)
            continue;

        table = (const uint32_t *)phys_to_virt(parent[directory] & PAGE_MASK);
        for (uint32_t index = 0; index < ENTRIES_PER_TABLE; index++) {
            uint32_t virt = (directory << 22) | (index << 12);
            uint32_t frame;

            if (table[index] == 0)
                continue;

            // Shared memory stays shared: the child gets the same frame.
            if (table[index] & PTE_SHARED) {
                frame = table[index] & PAGE_MASK;
                add_frame_user(frame);
                if (paging_map(child, virt, frame, table[index] & FLAGS_MASK) != 0) {
                    free_frame(frame);
                    delete_page_directory(child);
                    return nullptr;
                }
                continue;
            }

            frame = get_user_frame();
            if (frame == 0) {
                delete_page_directory(child);
                return nullptr;
            }

            // Getting a frame can sleep, and the swapper may have moved this
            // page meanwhile, so the entry must be read after that.
            uint32_t entry = table[index];
            uint32_t flags;

            if (entry & PTE_PRESENT) {
                memcpy(phys_to_virt(frame), phys_to_virt(entry & PAGE_MASK), PAGE_SIZE);
                flags = entry & FLAGS_MASK;
            } else if (entry & PTE_SWAPPED) {
                read_swapped_page(entry >> 12, frame);
                flags = (entry & (PTE_WRITABLE | PTE_USER)) | PTE_DIRTY;
            } else {
                free_frame(frame);      // it was clean and got dropped
                continue;
            }

            if (paging_map(child, virt, frame, flags) != 0) {
                free_frame(frame);
                delete_page_directory(child);
                return nullptr;
            }
            frame_set_owner(frame, child, virt);
        }
    }
    return child;
}

// Frees all user pages, their tables and the directory.
void delete_page_directory(uint32_t *page_dir)
{
    for (uint32_t directory = 0; directory < KERNEL_PD_START; directory++) {
        uint32_t *table;

        if ((page_dir[directory] & PTE_PRESENT) == 0)
            continue;

        table = (uint32_t *)phys_to_virt(page_dir[directory] & PAGE_MASK);
        for (uint32_t index = 0; index < ENTRIES_PER_TABLE; index++) {
            if (table[index] & PTE_PRESENT)
                free_frame(table[index] & PAGE_MASK);
            else if (table[index] & PTE_SWAPPED)
                swap_free_slot(table[index] >> 12);
        }
        free_frame(page_dir[directory] & PAGE_MASK);
    }
    free_frame(virt_to_phys(page_dir));
}

// Copies into an address space that may not be the active one.
int write_to_page_directory(uint32_t *page_dir, uint32_t virt, const void *source, uint32_t count)
{
    const uint8_t *input = (const uint8_t *)source;

    while (count > 0) {
        uint32_t entry = paging_lookup(page_dir, virt);
        uint32_t in_page = virt & FLAGS_MASK;
        uint32_t chunk = PAGE_SIZE - in_page;

        if ((entry & PTE_PRESENT) == 0)
            return -EFAULT;
        if (chunk > count)
            chunk = count;

        memcpy((uint8_t *)phys_to_virt(entry & PAGE_MASK) + in_page, input, chunk);
        virt += chunk;
        input += chunk;
        count -= chunk;
    }
    return 0;
}

// A bad pointer from a program must fail the system call, not crash the kernel,
// so every page is checked before the kernel touches it.
bool user_range_valid(const void *address, uint32_t count, bool write)
{
    uint32_t start = (uint32_t)address;
    uint32_t required = PTE_PRESENT | PTE_USER | (write ? PTE_WRITABLE : 0);

    if (start >= KERNEL_VMA || count > KERNEL_VMA - start)
        return false;

    for (uint32_t page = start & PAGE_MASK; page < start + count; page += PAGE_SIZE) {
        // Pages load on first use, so a page that isn't here yet may still be
        // fine. Bring it in now, while it's safe to sleep.
        if ((paging_lookup(current_process->page_dir, page) & required) != required &&
            !handle_page_fault(page, write))
            return false;
    }
    return true;
}

int copy_string_from_user(char *destination, const char *source, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        if (!user_range_valid(source + i, 1, false))
            return -EFAULT;

        destination[i] = source[i];
        if (source[i] == '\0')
            return 0;
    }
    return -ENAMETOOLONG;
}
