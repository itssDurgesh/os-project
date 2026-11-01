// ext2 filesystem driver. It follows the real on-disk format, so Linux tools
// can build the disk image and e2fsck can check our writes.
// One Mutex covers the whole filesystem. Each Ext2Inode function takes it on
// entry, and the static helpers assume it's already held.

#include <stddef.h>

#include "console.h"
#include "errno.h"
#include "hal.h"
#include "kstring.h"
#include "memory.h"
#include "storage.h"
#include "sync.h"
#include "vfs.h"

#define EXT2_MAGIC 0xEF53
#define SUPERBLOCK_OFFSET 1024
#define ROOT_INODE 2

#define DIRECT_BLOCKS 12
#define INDIRECT_SLOT 12
#define DOUBLE_INDIRECT_SLOT 13
#define POINTERS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))

#define MODE_TYPE_MASK 0xF000
#define MODE_DIRECTORY 0x4000
#define MODE_FILE      0x8000
#define MODE_PERMISSION_MASK 0x1FF

#define DIRENT_FILE 1
#define DIRENT_DIRECTORY 2
#define DIRENT_HEADER_SIZE 8

#define SECTORS_PER_FS_BLOCK (BLOCK_SIZE / 512)

struct ext2_superblock {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t reserved_blocks;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t log_fragment_size;
    uint32_t blocks_per_group;
    uint32_t fragments_per_group;
    uint32_t inodes_per_group;
    uint32_t mount_time;
    uint32_t write_time;
    uint16_t mount_count;
    uint16_t max_mount_count;
    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minor_revision;
    uint32_t last_check;
    uint32_t check_interval;
    uint32_t creator_os;
    uint32_t revision;
    uint16_t default_uid;
    uint16_t default_gid;
    uint32_t first_inode;
    uint16_t inode_size;
} __attribute__((packed));

struct ext2_group {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks;
    uint16_t free_inodes;
    uint16_t used_directories;
    uint16_t padding;
    uint8_t reserved[12];
} __attribute__((packed));

struct ext2_inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t access_time;
    uint32_t change_time;
    uint32_t modify_time;
    uint32_t delete_time;
    uint16_t gid;
    uint16_t links;
    uint32_t sectors;  // in 512-byte units
    uint32_t flags;
    uint32_t os_specific1;
    uint32_t blocks[15];  // the last slot is triple-indirect, unused here
    uint32_t generation;
    uint32_t file_acl;
    uint32_t directory_acl;
    uint32_t fragment_address;
    uint8_t os_specific2[12];
};

static_assert(sizeof(struct ext2_inode) == 128, "ext2 inode must be 128 bytes");
static_assert(sizeof(struct ext2_group) == 32, "ext2 group must be 32 bytes");

struct ext2_dirent {
    uint32_t inode;
    uint16_t entry_length;
    uint8_t name_length;
    uint8_t file_type;
    char name[];
} __attribute__((packed));

// An ext2 file or folder. `disk` is its copy of the inode from the disk.
class Ext2Inode final : public Inode {
public:
    struct ext2_inode disk;
    Ext2Inode *next_cached = nullptr;

    int read(void *buffer, uint32_t offset, uint32_t count) override;
    int write(const void *buffer, uint32_t offset, uint32_t count) override;
    int truncate() override;
    int lookup(const char *name, Inode **result) override;
    int create(const char *name, uint32_t mode, uint32_t uid, uint32_t gid,
               Inode **result) override;
    int mkdir(const char *name, uint32_t mode, uint32_t uid, uint32_t gid) override;
    int unlink(const char *name) override;
    int rmdir(const char *name) override;
    int readdir(uint32_t *offset, struct dirent *result) override;
    int sync() override;
    void release() override;
};

struct ext2_fs {
    int partition;
    struct ext2_superblock super;
    struct ext2_group *groups;
    uint32_t group_count;
    uint32_t group_table_blocks;
    uint32_t inode_size;
    Ext2Inode *cached_inodes;
    Mutex lock;
};

// One filesystem is mounted, so the driver keeps it here.
static struct ext2_fs fs;

// ext2 doesn't use the real-time clock, so times are seconds since boot. A nonzero
// value keeps e2fsck happy about deleted inodes.
static uint32_t now()
{
    return get_ticks() / TIMER_HZ + 1;
}

static struct ext2_inode *disk_inode(Ext2Inode *inode)
{
    return &inode->disk;
}

// Superblock and group descriptors

static int save_superblock()
{
    Buffer *buffer = block_get(fs.partition, 0);
    int result;

    fs.super.write_time = now();
    memcpy(buffer->data + SUPERBLOCK_OFFSET, &fs.super, sizeof(fs.super));
    result = block_write(buffer);
    block_put(buffer);
    return result;
}

static int save_group(uint32_t group)
{
    uint32_t per_block = BLOCK_SIZE / sizeof(struct ext2_group);
    uint32_t table_block = group / per_block;
    Buffer *buffer = block_get(fs.partition, 1 + table_block);
    int result;

    memcpy(buffer->data + (group % per_block) * sizeof(struct ext2_group),
           &fs.groups[group], sizeof(struct ext2_group));
    result = block_write(buffer);
    block_put(buffer);
    return result;
}

// Bitmaps

static int find_clear_bit(const uint8_t *bitmap, uint32_t bits)
{
    for (uint32_t i = 0; i < bits; i++) {
        if ((bitmap[i / 8] & (1 << (i % 8))) == 0)
            return (int)i;
    }
    return -1;
}

static int alloc_block(uint32_t *result)
{
    for (uint32_t group = 0; group < fs.group_count; group++) {
        if (fs.groups[group].free_blocks == 0)
            continue;

        Buffer *bitmap = block_get(fs.partition, fs.groups[group].block_bitmap);
        int bit = find_clear_bit(bitmap->data, fs.super.blocks_per_group);

        if (bit < 0) {
            block_put(bitmap);
            continue;
        }

        bitmap->data[bit / 8] |= 1 << (bit % 8);
        block_write(bitmap);
        block_put(bitmap);

        fs.groups[group].free_blocks--;
        fs.super.free_blocks--;
        save_group(group);
        save_superblock();

        *result = group * fs.super.blocks_per_group + bit + fs.super.first_data_block;

        // New blocks must start zeroed, or old data could leak into a file.
        Buffer *block = block_get(fs.partition, *result);

        memset(block->data, 0, BLOCK_SIZE);
        block_write(block);
        block_put(block);
        return 0;
    }
    return -ENOSPC;
}

static void free_block(uint32_t block)
{
    uint32_t index = block - fs.super.first_data_block;
    uint32_t group = index / fs.super.blocks_per_group;
    uint32_t bit = index % fs.super.blocks_per_group;
    Buffer *bitmap = block_get(fs.partition, fs.groups[group].block_bitmap);

    bitmap->data[bit / 8] &= ~(1 << (bit % 8));
    block_write(bitmap);
    block_put(bitmap);

    fs.groups[group].free_blocks++;
    fs.super.free_blocks++;
    save_group(group);
    save_superblock();
}

static int alloc_inode_number(bool directory, uint32_t *result)
{
    for (uint32_t group = 0; group < fs.group_count; group++) {
        if (fs.groups[group].free_inodes == 0)
            continue;

        Buffer *bitmap = block_get(fs.partition, fs.groups[group].inode_bitmap);
        int bit = find_clear_bit(bitmap->data, fs.super.inodes_per_group);

        if (bit < 0) {
            block_put(bitmap);
            continue;
        }

        bitmap->data[bit / 8] |= 1 << (bit % 8);
        block_write(bitmap);
        block_put(bitmap);

        fs.groups[group].free_inodes--;
        fs.super.free_inodes--;
        if (directory)
            fs.groups[group].used_directories++;
        save_group(group);
        save_superblock();

        *result = group * fs.super.inodes_per_group + bit + 1;
        return 0;
    }
    return -ENOSPC;
}

static void free_inode_number(uint32_t number, bool directory)
{
    uint32_t group = (number - 1) / fs.super.inodes_per_group;
    uint32_t bit = (number - 1) % fs.super.inodes_per_group;
    Buffer *bitmap = block_get(fs.partition, fs.groups[group].inode_bitmap);

    bitmap->data[bit / 8] &= ~(1 << (bit % 8));
    block_write(bitmap);
    block_put(bitmap);

    fs.groups[group].free_inodes++;
    fs.super.free_inodes++;
    if (directory)
        fs.groups[group].used_directories--;
    save_group(group);
    save_superblock();
}

// Inodes

static void inode_location(uint32_t number, uint32_t *block, uint32_t *offset)
{
    uint32_t group = (number - 1) / fs.super.inodes_per_group;
    uint32_t index = (number - 1) % fs.super.inodes_per_group;

    *block = fs.groups[group].inode_table + index * fs.inode_size / BLOCK_SIZE;
    *offset = index * fs.inode_size % BLOCK_SIZE;
}

static void load_disk_inode(uint32_t number, struct ext2_inode *result)
{
    uint32_t block, offset;
    Buffer *buffer;

    inode_location(number, &block, &offset);
    buffer = block_get(fs.partition, block);
    memcpy(result, buffer->data + offset, sizeof(*result));
    block_put(buffer);
}

static int save_inode(Ext2Inode *inode)
{
    struct ext2_inode *disk = disk_inode(inode);
    uint32_t block, offset;
    Buffer *buffer;
    int result;

    disk->mode = (disk->mode & MODE_TYPE_MASK) | inode->mode;
    disk->uid = inode->uid;
    disk->gid = inode->gid;
    disk->links = inode->links;
    disk->size = inode->size;

    inode_location(inode->number, &block, &offset);
    buffer = block_get(fs.partition, block);
    memcpy(buffer->data + offset, disk, sizeof(*disk));
    result = block_write(buffer);
    block_put(buffer);
    return result;
}

static Ext2Inode *wrap_inode(uint32_t number, const struct ext2_inode *source)
{
    Ext2Inode *inode = new Ext2Inode();

    memcpy(&inode->disk, source, sizeof(inode->disk));
    inode->number = number;
    if ((source->mode & MODE_TYPE_MASK) == MODE_DIRECTORY)
        inode->type = INODE_DIRECTORY;
    else
        inode->type = INODE_FILE;
    inode->mode = source->mode & MODE_PERMISSION_MASK;
    inode->uid = source->uid;
    inode->gid = source->gid;
    inode->links = source->links;
    inode->size = source->size;

    inode->next_cached = fs.cached_inodes;
    fs.cached_inodes = inode;
    return inode;
}

// Returns the one in-memory inode for this number, with a new reference.
static Ext2Inode *get_inode(uint32_t number)
{
    struct ext2_inode disk;

    for (Ext2Inode *inode = fs.cached_inodes; inode != nullptr; inode = inode->next_cached) {
        if (inode->number == number) {
            inode->refcount++;
            return inode;
        }
    }

    load_disk_inode(number, &disk);
    return wrap_inode(number, &disk);
}

// File block mapping: 12 direct, then indirect, then double-indirect.

static void charge_new_block(Ext2Inode *inode)
{
    disk_inode(inode)->sectors += SECTORS_PER_FS_BLOCK;
}

// Finds slot `index` in a block of pointers, adding a block if asked to.
static int follow_pointer(Ext2Inode *inode, uint32_t pointer_block, uint32_t index,
                          bool create, uint32_t *result)
{
    Buffer *buffer = block_get(fs.partition, pointer_block);
    uint32_t *slots = (uint32_t *)buffer->data;
    int status = 0;

    if (slots[index] == 0 && create) {
        status = alloc_block(&slots[index]);
        if (status == 0) {
            charge_new_block(inode);
            status = block_write(buffer);
        }
    }

    *result = slots[index];
    block_put(buffer);
    return status;
}

static int top_level_block(Ext2Inode *inode, uint32_t slot, bool create,
                           uint32_t *result)
{
    uint32_t *blocks = disk_inode(inode)->blocks;

    if (blocks[slot] == 0 && create) {
        int status = alloc_block(&blocks[slot]);

        if (status != 0)
            return status;
        charge_new_block(inode);
    }

    *result = blocks[slot];
    return 0;
}

// Turns a block number inside a file into a block number on disk. A result of 0
// means the file has no block there.
static int map_block(Ext2Inode *inode, uint32_t file_block, bool create,
                     uint32_t *result)
{
    uint32_t pointer;
    int status;

    if (file_block < DIRECT_BLOCKS)
        return top_level_block(inode, file_block, create, result);

    file_block -= DIRECT_BLOCKS;
    if (file_block < POINTERS_PER_BLOCK) {
        status = top_level_block(inode, INDIRECT_SLOT, create, &pointer);
        if (status != 0 || pointer == 0) {
            *result = 0;
            return status;
        }
        return follow_pointer(inode, pointer, file_block, create, result);
    }

    file_block -= POINTERS_PER_BLOCK;
    if (file_block < POINTERS_PER_BLOCK * POINTERS_PER_BLOCK) {
        status = top_level_block(inode, DOUBLE_INDIRECT_SLOT, create, &pointer);
        if (status != 0 || pointer == 0) {
            *result = 0;
            return status;
        }

        status = follow_pointer(inode, pointer, file_block / POINTERS_PER_BLOCK,
                                create, &pointer);
        if (status != 0 || pointer == 0) {
            *result = 0;
            return status;
        }
        return follow_pointer(inode, pointer, file_block % POINTERS_PER_BLOCK,
                              create, result);
    }

    return -EFBIG;
}

static void free_pointer_block(uint32_t block, int depth)
{
    Buffer *buffer;
    uint32_t *pointers;

    if (block == 0)
        return;

    // Too big for the 8 KB kernel stack, especially when this recurses.
    pointers = (uint32_t *)kmalloc(BLOCK_SIZE);
    if (pointers == nullptr)
        panic("no memory to free a file");

    buffer = block_get(fs.partition, block);
    memcpy(pointers, buffer->data, BLOCK_SIZE);
    block_put(buffer);

    for (uint32_t i = 0; i < POINTERS_PER_BLOCK; i++) {
        if (pointers[i] == 0)
            continue;

        if (depth > 1)
            free_pointer_block(pointers[i], depth - 1);
        else
            free_block(pointers[i]);
    }
    free_block(block);
    kfree(pointers);
}

// Frees every block of a file.
static void free_all_blocks(Ext2Inode *inode)
{
    struct ext2_inode *disk = disk_inode(inode);

    for (int i = 0; i < DIRECT_BLOCKS; i++) {
        if (disk->blocks[i] != 0)
            free_block(disk->blocks[i]);
    }
    free_pointer_block(disk->blocks[INDIRECT_SLOT], 1);
    free_pointer_block(disk->blocks[DOUBLE_INDIRECT_SLOT], 2);

    memset(disk->blocks, 0, sizeof(disk->blocks));
    disk->sectors = 0;
    inode->size = 0;
}

// File contents

static int read_data(Ext2Inode *inode, void *destination, uint32_t offset,
                     uint32_t count)
{
    uint8_t *output = (uint8_t *)destination;
    uint32_t done = 0;

    if (offset >= inode->size)
        return 0;
    if (count > inode->size - offset)
        count = inode->size - offset;

    while (done < count) {
        uint32_t position = offset + done;
        uint32_t in_block = position % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - in_block;
        uint32_t disk_block;
        int status;

        if (chunk > count - done)
            chunk = count - done;

        status = map_block(inode, position / BLOCK_SIZE, false, &disk_block);
        if (status != 0)
            return status;

        if (disk_block == 0) {
            memset(output + done, 0, chunk);  // a hole in the file
        } else {
            Buffer *buffer = block_get(fs.partition, disk_block);

            memcpy(output + done, buffer->data + in_block, chunk);
            block_put(buffer);
        }
        done += chunk;
    }
    return (int)done;
}

static int write_data(Ext2Inode *inode, const void *source, uint32_t offset,
                      uint32_t count)
{
    const uint8_t *input = (const uint8_t *)source;
    uint32_t done = 0;

    while (done < count) {
        uint32_t position = offset + done;
        uint32_t in_block = position % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - in_block;
        uint32_t disk_block;
        Buffer *buffer;
        int status;

        if (chunk > count - done)
            chunk = count - done;

        status = map_block(inode, position / BLOCK_SIZE, true, &disk_block);
        if (status != 0) {
            if (done == 0)
                return status;
            break;
        }

        buffer = block_get(fs.partition, disk_block);
        memcpy(buffer->data + in_block, input + done, chunk);
        status = block_write(buffer);
        block_put(buffer);
        if (status != 0)
            return status;

        done += chunk;
    }

    if (offset + done > inode->size)
        inode->size = offset + done;

    disk_inode(inode)->modify_time = now();
    save_inode(inode);
    return (int)done;
}

// Directories

static uint32_t entry_space(uint32_t name_length)
{
    return (DIRENT_HEADER_SIZE + name_length + 3) & ~3u;
}

// Finds a name. On success returns its inode number and leaves the entry's byte
// offset within the directory in *entry_offset.
static uint32_t find_entry(Ext2Inode *directory, const char *name,
                           uint32_t *entry_offset, uint32_t *previous_offset)
{
    uint32_t name_length = strlen(name);
    uint32_t file_offset = 0;

    while (file_offset < directory->size) {
        uint32_t disk_block;

        if (map_block(directory, file_offset / BLOCK_SIZE, false, &disk_block) != 0)
            return 0;

        Buffer *buffer = block_get(fs.partition, disk_block);
        uint32_t in_block = 0;
        uint32_t previous = 0;

        while (in_block < BLOCK_SIZE) {
            struct ext2_dirent *entry = (ext2_dirent *)(buffer->data + in_block);

            if (entry->inode != 0 && entry->name_length == name_length &&
                memcmp(entry->name, name, name_length) == 0) {
                uint32_t number = entry->inode;

                *entry_offset = file_offset + in_block;
                if (previous_offset != nullptr)
                    *previous_offset = file_offset + previous;
                block_put(buffer);
                return number;
            }

            previous = in_block;
            in_block += entry->entry_length;
        }

        block_put(buffer);
        file_offset += BLOCK_SIZE;
    }
    return 0;
}

static int write_entry(struct ext2_dirent *entry, uint32_t inode_number,
                       const char *name, uint8_t file_type)
{
    uint32_t name_length = strlen(name);

    entry->inode = inode_number;
    entry->name_length = name_length;
    entry->file_type = file_type;
    memcpy(entry->name, name, name_length);
    return 0;
}

static int add_entry(Ext2Inode *directory, const char *name,
                     uint32_t inode_number, uint8_t file_type)
{
    uint32_t needed = entry_space(strlen(name));
    uint32_t file_offset = 0;
    uint32_t disk_block;
    Buffer *buffer;
    int status;

    if (strlen(name) > MAX_FILE_NAME)
        return -ENAMETOOLONG;

    while (file_offset < directory->size) {
        status = map_block(directory, file_offset / BLOCK_SIZE, false, &disk_block);
        if (status != 0)
            return status;

        buffer = block_get(fs.partition, disk_block);

        uint32_t in_block = 0;

        while (in_block < BLOCK_SIZE) {
            struct ext2_dirent *entry = (ext2_dirent *)(buffer->data + in_block);
            uint32_t used = (entry->inode == 0) ? 0 : entry_space(entry->name_length);

            if (entry->entry_length - used >= needed) {
                if (used == 0) {
                    write_entry(entry, inode_number, name, file_type);
                } else {
                    struct ext2_dirent *added = (ext2_dirent *)(buffer->data + in_block + used);

                    added->entry_length = entry->entry_length - used;
                    entry->entry_length = used;
                    write_entry(added, inode_number, name, file_type);
                }
                status = block_write(buffer);
                block_put(buffer);
                return status;
            }
            in_block += entry->entry_length;
        }

        block_put(buffer);
        file_offset += BLOCK_SIZE;
    }

    // Every block is full, so the directory grows by one.
    status = map_block(directory, file_offset / BLOCK_SIZE, true, &disk_block);
    if (status != 0)
        return status;

    buffer = block_get(fs.partition, disk_block);

    struct ext2_dirent *entry = (ext2_dirent *)buffer->data;

    entry->entry_length = BLOCK_SIZE;
    write_entry(entry, inode_number, name, file_type);
    status = block_write(buffer);
    block_put(buffer);

    directory->size += BLOCK_SIZE;
    save_inode(directory);
    return status;
}

static int remove_entry(Ext2Inode *directory, uint32_t entry_offset,
                        uint32_t previous_offset)
{
    uint32_t disk_block;
    Buffer *buffer;
    int status = map_block(directory, entry_offset / BLOCK_SIZE, false, &disk_block);
    uint32_t in_block = entry_offset % BLOCK_SIZE;
    uint32_t previous = previous_offset % BLOCK_SIZE;

    if (status != 0)
        return status;

    buffer = block_get(fs.partition, disk_block);

    struct ext2_dirent *entry = (ext2_dirent *)(buffer->data + in_block);

    if (in_block == 0) {
        entry->inode = 0;  // the first entry in a block just goes blank
    } else {
        struct ext2_dirent *before = (ext2_dirent *)(buffer->data + previous);

        before->entry_length += entry->entry_length;
    }

    status = block_write(buffer);
    block_put(buffer);
    return status;
}

static bool directory_is_empty(Ext2Inode *directory)
{
    uint32_t file_offset = 0;

    while (file_offset < directory->size) {
        uint32_t disk_block;

        if (map_block(directory, file_offset / BLOCK_SIZE, false, &disk_block) != 0)
            return false;

        Buffer *buffer = block_get(fs.partition, disk_block);
        uint32_t in_block = 0;

        while (in_block < BLOCK_SIZE) {
            struct ext2_dirent *entry = (ext2_dirent *)(buffer->data + in_block);
            bool dot = entry->name_length == 1 && entry->name[0] == '.';
            bool dot_dot = entry->name_length == 2 && entry->name[0] == '.' &&
                           entry->name[1] == '.';

            if (entry->inode != 0 && !dot && !dot_dot) {
                block_put(buffer);
                return false;
            }
            in_block += entry->entry_length;
        }

        block_put(buffer);
        file_offset += BLOCK_SIZE;
    }
    return true;
}

static Ext2Inode *new_inode(uint32_t number, uint32_t mode, uint32_t uid,
                               uint32_t gid, uint16_t links)
{
    struct ext2_inode disk;
    uint32_t time = now();

    memset(&disk, 0, sizeof(disk));
    disk.mode = mode;
    disk.uid = uid;
    disk.gid = gid;
    disk.links = links;
    disk.access_time = time;
    disk.change_time = time;
    disk.modify_time = time;

    Ext2Inode *inode = wrap_inode(number, &disk);

    save_inode(inode);
    return inode;
}

// Operations called through the VFS

int Ext2Inode::read(void *buffer, uint32_t offset, uint32_t count)
{
    Ext2Inode *inode = this;
    int result;

    fs.lock.lock();
    result = read_data(inode, buffer, offset, count);
    fs.lock.unlock();
    return result;
}

int Ext2Inode::write(const void *buffer, uint32_t offset, uint32_t count)
{
    Ext2Inode *inode = this;
    int result;

    fs.lock.lock();
    result = write_data(inode, buffer, offset, count);
    fs.lock.unlock();
    return result;
}

int Ext2Inode::truncate()
{
    Ext2Inode *inode = this;
    fs.lock.lock();
    free_all_blocks(inode);
    save_inode(inode);
    fs.lock.unlock();
    return 0;
}

int Ext2Inode::lookup(const char *name, Inode **result)
{
    Ext2Inode *directory = this;
    uint32_t offset;
    uint32_t number;

    fs.lock.lock();
    number = find_entry(directory, name, &offset, nullptr);
    if (number != 0)
        *result = get_inode(number);
    fs.lock.unlock();

    return (number != 0) ? 0 : -ENOENT;
}

int Ext2Inode::create(const char *name, uint32_t mode, uint32_t uid, uint32_t gid, Inode **result)
{
    Ext2Inode *directory = this;
    uint32_t offset;
    uint32_t number;
    int status;

    fs.lock.lock();

    if (find_entry(directory, name, &offset, nullptr) != 0) {
        status = -EEXIST;
    } else {
        status = alloc_inode_number(false, &number);
        if (status == 0) {
            *result = new_inode(number, MODE_FILE | mode, uid, gid, 1);
            status = add_entry(directory, name, number, DIRENT_FILE);
        }
    }

    fs.lock.unlock();
    return status;
}

int Ext2Inode::mkdir(const char *name, uint32_t mode, uint32_t uid, uint32_t gid)
{
    Ext2Inode *directory = this;
    uint32_t offset;
    uint32_t number;
    uint32_t disk_block;
    Ext2Inode *created;
    Buffer *buffer;
    int status;

    fs.lock.lock();

    if (find_entry(directory, name, &offset, nullptr) != 0) {
        fs.lock.unlock();
        return -EEXIST;
    }

    status = alloc_inode_number(true, &number);
    if (status != 0) {
        fs.lock.unlock();
        return status;
    }

    created = new_inode(number, MODE_DIRECTORY | mode, uid, gid, 2);
    status = map_block(created, 0, true, &disk_block);
    if (status != 0) {
        created->links = 0;
        fs.lock.unlock();
        release_inode(created);  // takes fs.lock itself, to delete the inode
        return status;
    }

    buffer = block_get(fs.partition, disk_block);

    struct ext2_dirent *dot = (ext2_dirent *)buffer->data;
    struct ext2_dirent *dot_dot = (ext2_dirent *)(buffer->data + entry_space(1));

    dot->entry_length = entry_space(1);
    write_entry(dot, number, ".", DIRENT_DIRECTORY);
    dot_dot->entry_length = BLOCK_SIZE - entry_space(1);
    write_entry(dot_dot, directory->number, "..", DIRENT_DIRECTORY);
    block_write(buffer);
    block_put(buffer);

    created->size = BLOCK_SIZE;
    save_inode(created);

    status = add_entry(directory, name, number, DIRENT_DIRECTORY);
    directory->links++;
    save_inode(directory);

    fs.lock.unlock();
    release_inode(created);
    return status;
}

int Ext2Inode::unlink(const char *name)
{
    Ext2Inode *directory = this;
    uint32_t offset, previous;
    uint32_t number;
    Ext2Inode *inode;
    int status;

    fs.lock.lock();

    number = find_entry(directory, name, &offset, &previous);
    if (number == 0) {
        fs.lock.unlock();
        return -ENOENT;
    }

    inode = get_inode(number);
    if (inode->type == INODE_DIRECTORY) {
        fs.lock.unlock();
        release_inode(inode);
        return -EISDIR;
    }

    status = remove_entry(directory, offset, previous);
    inode->links--;
    save_inode(inode);

    fs.lock.unlock();
    release_inode(inode);  // the file really goes when the last user closes
    return status;
}

int Ext2Inode::rmdir(const char *name)
{
    Ext2Inode *directory = this;
    uint32_t offset, previous;
    uint32_t number;
    Ext2Inode *inode;
    int status = 0;

    fs.lock.lock();

    number = find_entry(directory, name, &offset, &previous);
    if (number == 0) {
        fs.lock.unlock();
        return -ENOENT;
    }

    inode = get_inode(number);
    if (inode->type != INODE_DIRECTORY) {
        status = -ENOTDIR;
    } else if (!directory_is_empty(inode)) {
        status = -ENOTEMPTY;
    } else {
        status = remove_entry(directory, offset, previous);
        inode->links = 0;
        directory->links--;
        save_inode(directory);
        save_inode(inode);
    }

    fs.lock.unlock();
    release_inode(inode);
    return status;
}

int Ext2Inode::readdir(uint32_t *offset, struct dirent *result)
{
    Ext2Inode *directory = this;
    int status = 0;

    fs.lock.lock();

    while (*offset < directory->size) {
        uint32_t disk_block;

        status = map_block(directory, *offset / BLOCK_SIZE, false, &disk_block);
        if (status != 0)
            break;

        Buffer *buffer = block_get(fs.partition, disk_block);
        struct ext2_dirent *entry = (ext2_dirent *)(buffer->data + *offset % BLOCK_SIZE);
        bool found = entry->inode != 0;

        if (found) {
            result->inode = entry->inode;
            result->type = (entry->file_type == DIRENT_DIRECTORY)
                               ? INODE_DIRECTORY : INODE_FILE;
            memcpy(result->name, entry->name, entry->name_length);
            result->name[entry->name_length] = '\0';
        }

        *offset += entry->entry_length;
        block_put(buffer);

        if (found) {
            status = 1;
            break;
        }
    }

    fs.lock.unlock();
    return status;
}

int Ext2Inode::sync()
{
    Ext2Inode *inode = this;
    int result;

    fs.lock.lock();
    result = save_inode(inode);
    fs.lock.unlock();
    return result;
}

// Runs when nobody holds the inode any more. A file with no names left is
// finally deleted here.
void Ext2Inode::release()
{
    Ext2Inode *inode = this;
    Ext2Inode **link;

    fs.lock.lock();

    for (link = &fs.cached_inodes; *link != nullptr; link = &(*link)->next_cached) {
        if (*link == inode) {
            *link = inode->next_cached;
            break;
        }
    }

    if (inode->links == 0) {
        bool directory = inode->type == INODE_DIRECTORY;

        free_all_blocks(inode);
        disk_inode(inode)->delete_time = now();
        save_inode(inode);
        free_inode_number(inode->number, directory);
    }

    fs.lock.unlock();
    delete inode;
}

void ext2_get_usage(uint32_t *total_kb, uint32_t *free_kb)
{
    uint32_t kb_per_block = BLOCK_SIZE / 1024;

    *total_kb = fs.super.blocks_count * kb_per_block;
    *free_kb = fs.super.free_blocks * kb_per_block;
}

Inode *ext2_mount(int partition)
{
    Buffer *buffer;
    uint32_t table_bytes;

    fs.partition = partition;

    buffer = block_get(partition, 0);
    memcpy(&fs.super, buffer->data + SUPERBLOCK_OFFSET, sizeof(fs.super));
    block_put(buffer);

    if (fs.super.magic != EXT2_MAGIC)
        panic("no ext2 filesystem on partition %d", partition);
    if ((1024u << fs.super.log_block_size) != BLOCK_SIZE)
        panic("ext2 block size must be 4 KB");

    fs.inode_size = (fs.super.revision == 0) ? 128 : fs.super.inode_size;
    fs.group_count = (fs.super.blocks_count - fs.super.first_data_block +
                      fs.super.blocks_per_group - 1) / fs.super.blocks_per_group;

    table_bytes = fs.group_count * sizeof(struct ext2_group);
    fs.groups = (ext2_group *)kmalloc(table_bytes);
    if (fs.groups == nullptr)
        panic("no memory for the ext2 group table");

    fs.group_table_blocks = round_up_to_page(table_bytes) / BLOCK_SIZE;
    for (uint32_t i = 0; i < fs.group_table_blocks; i++) {
        uint32_t left = table_bytes - i * BLOCK_SIZE;

        buffer = block_get(partition, 1 + i);
        memcpy((uint8_t *)fs.groups + i * BLOCK_SIZE, buffer->data,
               (left < BLOCK_SIZE) ? left : BLOCK_SIZE);
        block_put(buffer);
    }

    return get_inode(ROOT_INODE);
}
