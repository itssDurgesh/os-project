// Virtual memory: demand paging, swap and the Clock algorithm.
//
// A program's pages are not loaded when it starts. The first time it touches a
// page the CPU raises a page fault, and handle_page_fault() brings that page in.
//
// A page table entry can be in one of three states:
//   present        the page is in RAM
//   PTE_SWAPPED    the page is in swap, and the entry holds the slot number
//   zero           never loaded, so the page comes from a MemoryArea
//
// The swapper thread keeps memory free. It starts when RAM goes over 80% full and
// stops at 70%, choosing pages with Clock (the second-chance algorithm).
//
// Rule to avoid deadlock: the kernel never touches user memory while it holds a
// filesystem lock. Otherwise a page fault could wait for a lock that is held by
// the same process. That is why sys_read() copies through a kernel buffer.

#include "console.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "storage.h"
#include "sync.h"
#include "syscall.h"
#include "vfs.h"
#include "errno.h"

#define START_SWAPPING_PERCENT 80
#define STOP_SWAPPING_PERCENT 70
#define RESERVED_FRAMES 256     // always left for the kernel (1 MB)
#define KEEP_FLAGS (PTE_WRITABLE | PTE_USER)

static Mutex pager_lock;        // only one page moves in or out at a time
static WaitQueue swapper_wake_up;
static WaitQueue frames_freed;
static bool out_of_swap = false;
static uint32_t clock_hand = 0;
static uint32_t page_fault_count = 0;
static uint32_t evicted_page_count = 0;

static uint32_t percent_used()
{
    uint32_t total = total_frames();

    return (total - free_frame_count()) * 100 / total;
}

// Gets a frame for a user page. This can sleep while the swapper frees memory,
// so it must be called before taking pager_lock.
uint32_t get_user_frame()
{
    for (;;) {
        if (percent_used() >= START_SWAPPING_PERCENT)
            swapper_wake_up.wake_one();

        if (free_frame_count() > RESERVED_FRAMES)
            return alloc_frame();

        if (out_of_swap)
            return 0;

        frames_freed.sleep();
    }
}

// Removes one page from RAM. Returns false if there was nothing to remove.
static bool evict_one_page()
{
    uint32_t total = total_frames();

    // Two full turns: the first may only be clearing the "used recently" bits.
    for (uint32_t steps = 0; steps < total * 2; steps++) {
        uint32_t frame_number = clock_hand;
        uint32_t *page_dir;
        uint32_t virt;

        clock_hand = (clock_hand + 1) % total;

        if (!frame_get_owner(frame_number, &page_dir, &virt))
            continue;

        uint32_t *entry = paging_get_entry(page_dir, virt);

        if (entry == nullptr || (*entry & PTE_PRESENT) == 0)
            continue;

        // Used recently, so it gets a second chance.
        if (*entry & PTE_ACCESSED) {
            *entry = *entry & ~PTE_ACCESSED;
            continue;
        }

        uint32_t frame = frame_number * PAGE_SIZE;

        if (*entry & PTE_DIRTY) {
            int slot = swap_alloc_slot();

            if (slot < 0)
                continue;       // swap is full, but a clean page can still go

            // Change the entry first, so a fault on this page has to wait for
            // pager_lock until the write has finished.
            *entry = ((uint32_t)slot << 12) | PTE_SWAPPED | (*entry & KEEP_FLAGS);
            swap_write(slot, phys_to_virt(frame));
        } else {
            // A clean page can be loaded again from its file, or is still zeros.
            *entry = 0;
        }

        free_frame(frame);
        evicted_page_count++;
        return true;
    }
    return false;
}

static void swapper_thread(void *)
{
    for (;;) {
        while (percent_used() < START_SWAPPING_PERCENT || out_of_swap)
            swapper_wake_up.sleep();

        while (percent_used() >= STOP_SWAPPING_PERCENT) {
            pager_lock.lock();
            bool removed_a_page = evict_one_page();
            pager_lock.unlock();

            frames_freed.wake_all();
            if (!removed_a_page) {
                out_of_swap = true;
                break;
            }
        }
    }
}

void memory_page_returned_to_swap()
{
    out_of_swap = false;
}

static MemoryArea *find_area(Process *process, uint32_t address)
{
    for (int i = 0; i < MAX_MEMORY_AREAS; i++) {
        MemoryArea *area = &process->areas[i];

        if (area->in_use && address >= area->start && address < area->end)
            return area;
    }
    return nullptr;
}

// Fills a new frame with what belongs at this address.
static bool load_page(MemoryArea *area, uint32_t page, uint32_t frame)
{
    uint32_t position = page - area->start;

    memset(phys_to_virt(frame), 0, PAGE_SIZE);

    if (position < area->file_size) {
        uint32_t bytes = area->file_size - position;

        if (bytes > PAGE_SIZE)
            bytes = PAGE_SIZE;

        if (area->file->read(phys_to_virt(frame), area->file_offset + position,
                             bytes) < 0)
            return false;
    }
    return true;
}

static void connect_page(uint32_t *page_dir, uint32_t page, uint32_t frame, uint32_t flags)
{
    paging_map(page_dir, page, frame, flags);
    frame_set_owner(frame, page_dir, page);
}

// Returns true if the page is now there and the program can carry on.
bool handle_page_fault(uint32_t address, bool is_write)
{
    uint32_t *page_dir = current_process->page_dir;
    uint32_t page = address & PAGE_MASK;
    uint32_t frame = get_user_frame();
    bool worked = false;

    if (frame == 0) {
        kprintf("%s: out of memory\n", current_process->name);
        return false;
    }

    pager_lock.lock();
    page_fault_count++;

    uint32_t entry = paging_lookup(page_dir, page);

    if (entry & PTE_PRESENT) {
        // Another fault already brought it in, or this was a bad write.
        worked = !is_write || (entry & PTE_WRITABLE);
        free_frame(frame);
    } else if (entry & PTE_SWAPPED) {
        uint32_t slot = entry >> 12;

        if (is_write && (entry & PTE_WRITABLE) == 0) {
            free_frame(frame);
        } else {
            swap_read(slot, phys_to_virt(frame));
            swap_free_slot(slot);

            // Dirty, because this page is no longer anywhere else.
            connect_page(page_dir, page, frame, (entry & KEEP_FLAGS) | PTE_DIRTY);
            worked = true;
        }
    } else {
        MemoryArea *area = find_area(current_process, page);

        if (area != nullptr && (!is_write || (area->flags & PTE_WRITABLE)) &&
            load_page(area, page, frame)) {
            connect_page(page_dir, page, frame, area->flags);
            worked = true;
        } else {
            free_frame(frame);
        }
    }

    pager_lock.unlock();
    return worked;
}

// Adds a page straight away. Used for the first stack pages, which exec() fills.
int map_new_user_page(uint32_t *page_dir, uint32_t virt, uint32_t flags)
{
    uint32_t frame = get_user_frame();

    if (frame == 0)
        return -ENOMEM;

    memset(phys_to_virt(frame), 0, PAGE_SIZE);

    // The kernel fills this page through its own address, so the CPU would not
    // mark it dirty. It has to be marked here, or eviction would lose the data.
    connect_page(page_dir, virt & PAGE_MASK, frame, flags | PTE_DIRTY);
    return 0;
}

// Removes one page from an address space, wherever it is at the moment.
void unmap_user_page(uint32_t *page_dir, uint32_t virt)
{
    uint32_t *entry = paging_get_entry(page_dir, virt);

    if (entry == nullptr || *entry == 0)
        return;

    if (*entry & PTE_SWAPPED) {
        swap_free_slot(*entry >> 12);
        *entry = 0;
    } else if (*entry & PTE_PRESENT) {
        uint32_t frame = paging_unmap(page_dir, virt);

        if (frame != 0)
            free_frame(frame);
    }
}

// For fork: reads a swapped page of the parent into the child's new frame.
void read_swapped_page(uint32_t slot, uint32_t frame)
{
    pager_lock.lock();
    swap_read(slot, phys_to_virt(frame));
    pager_lock.unlock();
}

int add_memory_area(MemoryArea *areas, uint32_t start, uint32_t end, uint32_t flags,
                    Inode *file, uint32_t file_offset, uint32_t file_size)
{
    for (int i = 0; i < MAX_MEMORY_AREAS; i++) {
        if (!areas[i].in_use) {
            areas[i].in_use = true;
            areas[i].start = start;
            areas[i].end = end;
            areas[i].flags = flags;
            areas[i].file = file;
            areas[i].file_offset = file_offset;
            areas[i].file_size = file_size;

            if (file != nullptr)
                hold_inode(file);
            return i;
        }
    }
    return -ENOMEM;
}

void release_memory_areas(MemoryArea *areas)
{
    for (int i = 0; i < MAX_MEMORY_AREAS; i++) {
        if (areas[i].in_use && areas[i].file != nullptr)
            release_inode(areas[i].file);
        areas[i].in_use = false;
    }
}

void copy_memory_areas(const MemoryArea *from, MemoryArea *to)
{
    for (int i = 0; i < MAX_MEMORY_AREAS; i++) {
        to[i] = from[i];
        if (to[i].in_use && to[i].file != nullptr)
            hold_inode(to[i].file);
    }
}

// Reports memory use, for the `free` command.
int sys_meminfo(Registers *r)
{
    MemoryInfo *output = (MemoryInfo *)r->ebx;
    MemoryInfo info;

    if (!user_range_valid(output, sizeof(*output), true))
        return -EFAULT;

    info.total_frames = total_frames();
    info.free_frames = free_frame_count();
    info.swap_slots_total = partition_blocks(PARTITION_SWAP);
    info.swap_slots_used = swap_used_slots();
    info.page_faults = page_fault_count;
    info.evicted_pages = evicted_page_count;

    memcpy(output, &info, sizeof(info));
    return 0;
}

// Step 9 of the boot order.
void virtual_memory_init()
{
    swap_init();
    thread_create("swapper", 1, swapper_thread, nullptr);
}
