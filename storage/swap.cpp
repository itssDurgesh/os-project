// The swap partition: 32 MB split into 4 KB slots, with one bit per slot to say
// if it is used. Swap has no files or folders, so it talks to the disk directly
// and doesn't use the buffer cache.

#include "console.h"
#include "memory.h"
#include "storage.h"

#define BITS_PER_BYTE 8
#define MAX_SWAP_SLOTS 32768    // enough for 128 MB of swap

static uint8_t slot_used[MAX_SWAP_SLOTS / BITS_PER_BYTE];
static uint32_t slot_count;
static uint32_t used_count;
static uint32_t search_from;

void swap_init()
{
    slot_count = partition_blocks(PARTITION_SWAP);
    if (slot_count > MAX_SWAP_SLOTS)
        slot_count = MAX_SWAP_SLOTS;
}

int swap_alloc_slot()
{
    for (uint32_t i = 0; i < slot_count; i++) {
        uint32_t slot = (search_from + i) % slot_count;
        uint8_t bit = 1 << (slot % BITS_PER_BYTE);

        if ((slot_used[slot / BITS_PER_BYTE] & bit) == 0) {
            slot_used[slot / BITS_PER_BYTE] |= bit;
            used_count++;
            search_from = slot + 1;
            return (int)slot;
        }
    }
    return -1;
}

void swap_free_slot(uint32_t slot)
{
    uint8_t bit = 1 << (slot % BITS_PER_BYTE);

    if (slot >= slot_count || (slot_used[slot / BITS_PER_BYTE] & bit) == 0)
        panic("freeing swap slot %u that isn't in use", slot);

    slot_used[slot / BITS_PER_BYTE] &= ~bit;
    used_count--;
    memory_page_returned_to_swap();
}

uint32_t swap_used_slots()
{
    return used_count;
}

void swap_read(uint32_t slot, void *page)
{
    if (ata_read(partition_first_sector(PARTITION_SWAP) + slot * SECTORS_PER_BLOCK,
                 SECTORS_PER_BLOCK, page) != 0)
        panic("swap read failed at slot %u", slot);
}

void swap_write(uint32_t slot, const void *page)
{
    // No flush: swap only has to last until the next reboot.
    if (ata_write(partition_first_sector(PARTITION_SWAP) + slot * SECTORS_PER_BLOCK,
                  SECTORS_PER_BLOCK, page, false) != 0)
        panic("swap write failed at slot %u", slot);
}
