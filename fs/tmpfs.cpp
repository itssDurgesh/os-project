// tmpfs: a filesystem kept completely in RAM, mounted on /tmp. It is much simpler
// than ext2 because there are no blocks. A file is one chunk of memory that grows
// when needed, and a folder is a linked list. Everything is lost at reboot.

#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "vfs.h"

#define TMP_FOLDER_MODE 0777
#define FIRST_CAPACITY 256

class TmpInode final : public Inode {
public:
    // For files
    char *data = nullptr;
    uint32_t capacity = 0;

    // For folders
    TmpInode *parent = nullptr;
    TmpInode *first_child = nullptr;

    // For items inside a folder
    TmpInode *next_sibling = nullptr;
    char name[MAX_FILE_NAME + 1];

    int read(void *buffer, uint32_t offset, uint32_t count) override;
    int write(const void *buffer, uint32_t offset, uint32_t count) override;
    int truncate() override;
    int lookup(const char *name, Inode **result) override;
    int create(const char *name, uint32_t mode, uint32_t uid, uint32_t gid,
               Inode **result) override;
    int mkdir(const char *name, uint32_t mode, uint32_t uid, uint32_t gid) override;
    int unlink(const char *name) override;
    int rmdir(const char *name) override;
    int readdir(uint32_t *offset, struct dirent *entry) override;
    int sync() override;
    void release() override;

private:
    TmpInode *find_child(const char *child_name);
    TmpInode *add_child(const char *child_name, InodeType new_type, uint32_t new_mode,
                        uint32_t new_uid, uint32_t new_gid);
    void remove_child(TmpInode *child);
    bool make_room_for(uint32_t needed);
};

static uint32_t next_inode_number = 1;

TmpInode *TmpInode::find_child(const char *child_name)
{
    for (TmpInode *child = first_child; child != nullptr; child = child->next_sibling) {
        if (strcmp(child->name, child_name) == 0)
            return child;
    }
    return nullptr;
}

// The new item starts with one reference, which belongs to its folder.
TmpInode *TmpInode::add_child(const char *child_name, InodeType new_type,
                              uint32_t new_mode, uint32_t new_uid, uint32_t new_gid)
{
    TmpInode *child = new TmpInode();

    child->number = next_inode_number;
    next_inode_number++;
    child->type = new_type;
    child->mode = new_mode;
    child->uid = new_uid;
    child->gid = new_gid;
    child->links = 1;
    child->parent = this;
    copy_string(child->name, child_name, sizeof(child->name));

    child->next_sibling = first_child;
    first_child = child;
    return child;
}

void TmpInode::remove_child(TmpInode *child)
{
    TmpInode **link = &first_child;

    while (*link != child)
        link = &(*link)->next_sibling;

    *link = child->next_sibling;
    child->links = 0;
    release_inode(child);       // drops the reference the folder was holding
}

// Makes the file's memory bigger, keeping what is already there.
bool TmpInode::make_room_for(uint32_t needed)
{
    if (needed <= capacity)
        return true;

    uint32_t new_capacity = (capacity == 0) ? FIRST_CAPACITY : capacity;

    while (new_capacity < needed)
        new_capacity = new_capacity * 2;

    char *new_data = (char *)kmalloc(new_capacity);

    if (new_data == nullptr)
        return false;

    if (data != nullptr) {
        memcpy(new_data, data, size);
        kfree(data);
    }

    data = new_data;
    capacity = new_capacity;
    return true;
}

int TmpInode::read(void *buffer, uint32_t offset, uint32_t count)
{
    if (offset >= size)
        return 0;

    if (count > size - offset)
        count = size - offset;

    memcpy(buffer, data + offset, count);
    return (int)count;
}

int TmpInode::write(const void *buffer, uint32_t offset, uint32_t count)
{
    if (!make_room_for(offset + count))
        return -ENOSPC;

    // Writing past the end leaves a gap, which must read back as zeros.
    if (offset > size)
        memset(data + size, 0, offset - size);

    memcpy(data + offset, buffer, count);
    if (offset + count > size)
        size = offset + count;
    return (int)count;
}

int TmpInode::truncate()
{
    size = 0;
    return 0;
}

int TmpInode::lookup(const char *child_name, Inode **result)
{
    Inode *found = nullptr;

    if (strcmp(child_name, ".") == 0) {
        found = this;
    } else if (strcmp(child_name, "..") == 0) {
        if (parent != nullptr)
            found = parent;
        else
            found = vfs_root();
    } else {
        found = find_child(child_name);
    }

    if (found == nullptr)
        return -ENOENT;

    hold_inode(found);
    *result = found;
    return 0;
}

int TmpInode::create(const char *child_name, uint32_t new_mode, uint32_t new_uid,
                     uint32_t new_gid, Inode **result)
{
    if (find_child(child_name) != nullptr)
        return -EEXIST;

    TmpInode *child = add_child(child_name, INODE_FILE, new_mode, new_uid, new_gid);

    hold_inode(child);
    *result = child;
    return 0;
}

int TmpInode::mkdir(const char *child_name, uint32_t new_mode, uint32_t new_uid,
                    uint32_t new_gid)
{
    if (find_child(child_name) != nullptr)
        return -EEXIST;

    add_child(child_name, INODE_DIRECTORY, new_mode, new_uid, new_gid);
    return 0;
}

int TmpInode::unlink(const char *child_name)
{
    TmpInode *child = find_child(child_name);

    if (child == nullptr)
        return -ENOENT;
    if (child->type == INODE_DIRECTORY)
        return -EISDIR;

    remove_child(child);
    return 0;
}

int TmpInode::rmdir(const char *child_name)
{
    TmpInode *child = find_child(child_name);

    if (child == nullptr)
        return -ENOENT;
    if (child->type != INODE_DIRECTORY)
        return -ENOTDIR;
    if (child->first_child != nullptr)
        return -ENOTEMPTY;

    remove_child(child);
    return 0;
}

int TmpInode::readdir(uint32_t *offset, struct dirent *entry)
{
    TmpInode *child = first_child;

    for (uint32_t i = 0; i < *offset && child != nullptr; i++)
        child = child->next_sibling;

    if (child == nullptr)
        return 0;

    entry->inode = child->number;
    entry->type = child->type;
    copy_string(entry->name, child->name, sizeof(entry->name));
    *offset = *offset + 1;
    return 1;
}

int TmpInode::sync()
{
    return 0;                   // nothing to save, because it only lives in RAM
}

// A deleted file stays until the last person using it has closed it.
void TmpInode::release()
{
    if (links == 0) {
        kfree(data);
        delete this;
    }
}

Inode *tmpfs_mount()
{
    TmpInode *root = new TmpInode();

    root->number = next_inode_number;
    next_inode_number++;
    root->type = INODE_DIRECTORY;
    root->mode = TMP_FOLDER_MODE;
    root->links = 2;
    return root;
}
