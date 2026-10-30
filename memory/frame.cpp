// Physical frame allocator: a bitmap with one bit per 4 KB frame, plus a frame
// table that records how many users each frame has.
// Only kernel code allocates memory, never an interrupt handler, and the kernel
// isn't preempted, so these functions don't need a lock.

#include <stdbool.h>

#include "console.h"
#include "kstring.h"
#include "memory.h"
#include "multiboot.h"

#define BITS_PER_WORD 32
#define MAX_PHYS_MEMORY 0x30000000u  // 768 MB fits in the kernel's 1 GB

struct Frame {
    uint16_t refcount;
    uint16_t block_pages;  // size of an alloc_frames() block
    uint32_t *owner_page_dir;  // set for a user page that can be swapped out
    uint32_t owner_virt;
};

extern "C" char _kernel_end[];

static uint32_t *bitmap;  // bit set = frame in use
static Frame *frame_table;
static uint32_t frame_count;
static uint32_t free_count;
static uint32_t search_start;  // no free frame exists below this

static bool frame_in_use(uint32_t index)
{
    return bitmap[index / BITS_PER_WORD] & (1u << (index % BITS_PER_WORD));
}

static void mark_used(uint32_t index)
{
    bitmap[index / BITS_PER_WORD] |= 1u << (index % BITS_PER_WORD);
    free_count--;
}

static void mark_free(uint32_t index)
{
    bitmap[index / BITS_PER_WORD] &= ~(1u << (index % BITS_PER_WORD));
    free_count++;
    if (index < search_start)
        search_start = index;
}

static const struct multiboot_mmap_entry *next_entry(
    const struct multiboot_mmap_entry *entry)
{
    return (const struct multiboot_mmap_entry *)((const char *)entry + entry->size + sizeof(entry->size));
}

static uint32_t find_memory_top(const struct multiboot_info *mbi)
{
    const struct multiboot_mmap_entry *entry = (const multiboot_mmap_entry *)phys_to_virt(mbi->mmap_addr);
    const void *end = (const char *)entry + mbi->mmap_length;
    uint64_t top = 0;

    for (; (const void *)entry < end; entry = next_entry(entry)) {
        uint64_t region_end = entry->addr + entry->length;

        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE && region_end > top)
            top = region_end;
    }

    return (top > MAX_PHYS_MEMORY) ? MAX_PHYS_MEMORY : (uint32_t)top;
}

static void free_available_regions(const struct multiboot_info *mbi)
{
    const struct multiboot_mmap_entry *entry = (const multiboot_mmap_entry *)phys_to_virt(mbi->mmap_addr);
    const void *end = (const char *)entry + mbi->mmap_length;

    for (; (const void *)entry < end; entry = next_entry(entry)) {
        if (entry->type != MULTIBOOT_MEMORY_AVAILABLE)
            continue;

        for (uint64_t address = entry->addr;
             address + PAGE_SIZE <= entry->addr + entry->length;
             address += PAGE_SIZE) {
            uint32_t index = address / PAGE_SIZE;

            if (address % PAGE_SIZE == 0 && index < frame_count)
                mark_free(index);
        }
    }
}

void frame_init(const struct multiboot_info *mbi)
{
    if ((mbi->flags & MULTIBOOT_FLAG_MMAP) == 0)
        panic("bootloader gave no memory map");

    frame_count = find_memory_top(mbi) / PAGE_SIZE;

    // The bitmap and frame table go straight after the kernel image.
    uint32_t bitmap_bytes = (frame_count / BITS_PER_WORD + 1) * sizeof(uint32_t);
    uint32_t table_bytes = frame_count * sizeof(Frame);
    uint32_t next_free = round_up_to_page(virt_to_phys(_kernel_end));

    bitmap = (uint32_t *)phys_to_virt(next_free);
    next_free = round_up_to_page(next_free + bitmap_bytes);
    frame_table = (Frame *)phys_to_virt(next_free);
    next_free = round_up_to_page(next_free + table_bytes);

    memset(bitmap, 0xFF, bitmap_bytes);
    memset(frame_table, 0, table_bytes);

    free_count = 0;
    search_start = frame_count;
    free_available_regions(mbi);

    // Low memory holds the BIOS data and VGA, and then come the kernel and the
    // tables above. None of it may ever be handed out.
    for (uint32_t index = 0; index < next_free / PAGE_SIZE; index++) {
        if (!frame_in_use(index))
            mark_used(index);
    }
    search_start = next_free / PAGE_SIZE;
}

uint32_t alloc_frame()
{
    for (uint32_t index = search_start; index < frame_count; index++) {
        if (!frame_in_use(index)) {
            mark_used(index);
            frame_table[index].refcount = 1;
            frame_table[index].block_pages = 1;
            search_start = index + 1;
            return index * PAGE_SIZE;
        }
    }
    return 0;
}

uint32_t alloc_frames(uint32_t count)
{
    uint32_t run = 0;

    for (uint32_t index = search_start; index < frame_count; index++) {
        run = frame_in_use(index) ? 0 : run + 1;
        if (run < count)
            continue;

        uint32_t first = index + 1 - count;

        for (uint32_t i = first; i <= index; i++) {
            mark_used(i);
            frame_table[i].refcount = 1;
            frame_table[i].block_pages = 1;
        }
        frame_table[first].block_pages = count;
        return first * PAGE_SIZE;
    }
    return 0;
}

void free_frame(uint32_t address)
{
    uint32_t index = address / PAGE_SIZE;

    if (index >= frame_count || !frame_in_use(index) ||
        frame_table[index].refcount == 0)
        panic("free_frame: bad frame %x", address);

    frame_table[index].refcount--;
    if (frame_table[index].refcount == 0) {
        frame_table[index].owner_page_dir = nullptr;
        mark_free(index);
    }
}

void free_frames(uint32_t address)
{
    uint32_t count = frame_table[address / PAGE_SIZE].block_pages;

    for (uint32_t i = 0; i < count; i++)
        free_frame(address + i * PAGE_SIZE);
}

void add_frame_user(uint32_t address)
{
    frame_table[address / PAGE_SIZE].refcount++;
}

uint32_t frame_user_count(uint32_t address)
{
    return frame_table[address / PAGE_SIZE].refcount;
}

void frame_set_owner(uint32_t address, uint32_t *page_dir, uint32_t virt)
{
    frame_table[address / PAGE_SIZE].owner_page_dir = page_dir;
    frame_table[address / PAGE_SIZE].owner_virt = virt;
}

// Tells the Clock algorithm whose page is in this frame. Pages with more than one
// user are shared, so they stay in RAM.
bool frame_get_owner(uint32_t frame_number, uint32_t **page_dir, uint32_t *virt)
{
    Frame *frame = &frame_table[frame_number];

    if (frame->owner_page_dir == nullptr || frame->refcount != 1)
        return false;

    *page_dir = frame->owner_page_dir;
    *virt = frame->owner_virt;
    return true;
}

uint32_t total_frames()
{
    return frame_count;
}

uint32_t free_frame_count()
{
    return free_count;
}
