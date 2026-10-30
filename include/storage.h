/* Storage Hierarchy: disk driver, partitions and buffer cache.
 * The ATA driver belongs to Resource Management, the rest to Storage. */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "process.h"

#define SECTOR_SIZE 512
#define BLOCK_SIZE  4096
#define SECTORS_PER_BLOCK (BLOCK_SIZE / SECTOR_SIZE)

enum partition_id {
    PARTITION_KERNEL,
    PARTITION_SWAP,
    PARTITION_DATA,
    PARTITION_COUNT,
};

/* One cached disk block. Block numbers count from the start of the partition. */
struct buffer {
    int partition;
    uint32_t block;
    bool valid;
    bool busy;                  /* a thread is using this buffer */
    uint32_t last_used;
    struct wait_queue waiters;
    uint8_t *data;
};

/* ATA driver */
void ata_init(void);
int ata_read(uint32_t lba, uint32_t sectors, void *buffer);
int ata_write(uint32_t lba, uint32_t sectors, const void *buffer);

/* Partitions and buffer cache */
void storage_init(void);
uint32_t partition_blocks(int partition);

/* Returns the block, read from disk if needed, reserved for the caller. */
struct buffer *block_get(int partition, uint32_t block);
void block_put(struct buffer *buffer);

/* Write-through: the data reaches the disk before this returns. */
int block_write(struct buffer *buffer);

uint32_t block_disk_reads(void);

#endif
