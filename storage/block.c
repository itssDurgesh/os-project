/* Partition table and disk buffer cache.
 *
 * The cache keeps recently used disk blocks in RAM. It's write-through: a write
 * goes to the disk straight away, so a crash loses as little as possible. When
 * the cache is full, the least recently used free buffer is reused. */

#include "console.h"
#include "kstring.h"
#include "memory.h"
#include "storage.h"

#define BUFFER_COUNT 128

#define PARTITION_TABLE_OFFSET 446
#define BOOT_SIGNATURE_OFFSET  510
#define BOOT_SIGNATURE         0xAA55
#define MBR_PARTITIONS         4

struct mbr_entry {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t first_sector;
    uint32_t sector_count;
} __attribute__((packed));

struct partition {
    uint32_t first_sector;
    uint32_t sector_count;
};

static struct partition partitions[PARTITION_COUNT];
static struct buffer buffers[BUFFER_COUNT];
static uint32_t use_counter;
static uint32_t disk_reads;

static uint32_t block_lba(int partition, uint32_t block)
{
    if (block >= partition_blocks(partition))
        panic("block %u is outside partition %d", block, partition);

    return partitions[partition].first_sector + block * SECTORS_PER_BLOCK;
}

static struct buffer *find_buffer(int partition, uint32_t block)
{
    for (int i = 0; i < BUFFER_COUNT; i++) {
        struct buffer *buffer = &buffers[i];

        if (buffer->valid && buffer->partition == partition &&
            buffer->block == block)
            return buffer;
    }
    return NULL;
}

static struct buffer *find_reusable_buffer(void)
{
    struct buffer *oldest = NULL;

    for (int i = 0; i < BUFFER_COUNT; i++) {
        struct buffer *buffer = &buffers[i];

        if (buffer->busy)
            continue;
        if (!buffer->valid)
            return buffer;
        if (oldest == NULL || buffer->last_used < oldest->last_used)
            oldest = buffer;
    }
    return oldest;
}

struct buffer *block_get(int partition, uint32_t block)
{
    struct buffer *buffer;

    for (;;) {
        buffer = find_buffer(partition, block);

        if (buffer == NULL) {
            buffer = find_reusable_buffer();
            if (buffer == NULL)
                panic("every disk buffer is in use");

            buffer->partition = partition;
            buffer->block = block;
            buffer->valid = false;
            buffer->busy = true;

            if (ata_read(block_lba(partition, block), SECTORS_PER_BLOCK,
                         buffer->data) != 0)
                panic("disk read failed at block %u", block);

            disk_reads++;
            buffer->valid = true;
            break;
        }

        if (!buffer->busy) {
            buffer->busy = true;
            break;
        }

        /* Someone else is using it, so wait and look again, because it may
         * have been reused for another block by the time we wake. */
        wait_queue_sleep(&buffer->waiters);
    }

    buffer->last_used = ++use_counter;
    return buffer;
}

void block_put(struct buffer *buffer)
{
    buffer->busy = false;
    wait_queue_wake_all(&buffer->waiters);
}

int block_write(struct buffer *buffer)
{
    return ata_write(block_lba(buffer->partition, buffer->block),
                     SECTORS_PER_BLOCK, buffer->data);
}

uint32_t block_disk_reads(void)
{
    return disk_reads;
}

uint32_t partition_blocks(int partition)
{
    return partitions[partition].sector_count / SECTORS_PER_BLOCK;
}

static void read_partition_table(void)
{
    uint8_t *sector = kmalloc(SECTOR_SIZE);
    struct mbr_entry entries[MBR_PARTITIONS];

    if (sector == NULL || ata_read(0, 1, sector) != 0)
        panic("can't read the partition table");

    if (*(uint16_t *)(sector + BOOT_SIGNATURE_OFFSET) != BOOT_SIGNATURE)
        panic("the disk has no partition table");

    memcpy(entries, sector + PARTITION_TABLE_OFFSET, sizeof(entries));
    for (int i = 0; i < PARTITION_COUNT; i++) {
        if (entries[i].sector_count == 0)
            panic("partition %d is missing", i + 1);

        partitions[i].first_sector = entries[i].first_sector;
        partitions[i].sector_count = entries[i].sector_count;
    }

    kfree(sector);
}

void storage_init(void)
{
    uint32_t frames = alloc_frames(BUFFER_COUNT);

    if (frames == 0)
        panic("no memory for the buffer cache");

    for (int i = 0; i < BUFFER_COUNT; i++) {
        buffers[i].data = (uint8_t *)P2V(frames) + i * BLOCK_SIZE;
        wait_queue_init(&buffers[i].waiters);
    }

    read_partition_table();
}
