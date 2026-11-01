// The global open-file table. Each File holds a position, the access mode and a
// count of how many descriptors point at it.

#include <stddef.h>

#include "errno.h"
#include "memory.h"
#include "vfs.h"

// A pipe overrides these. The base versions are for a File opened the wrong way.
int File::read(void *, uint32_t)
{
    return -EBADF;
}

int File::write(const void *, uint32_t)
{
    return -EBADF;
}

void File::close()
{
}

// A File that is really a file or device, so it passes everything to its inode.
class InodeFile : public File {
public:
    int read(void *buffer, uint32_t count) override;
    int write(const void *buffer, uint32_t count) override;
    void close() override;
};

int InodeFile::read(void *buffer, uint32_t count)
{
    int result = inode->read(buffer, offset, count);

    if (result > 0)
        offset += result;
    return result;
}

int InodeFile::write(const void *buffer, uint32_t count)
{
    int result;

    if (flags & O_APPEND)
        offset = inode->size;

    result = inode->write(buffer, offset, count);
    if (result > 0)
        offset += result;
    return result;
}

void InodeFile::close()
{
    release_inode(inode);
}

int file_open(Inode *base, const char *path, uint32_t flags, uint32_t mode,
              uint32_t uid, uint32_t gid, File **result)
{
    uint32_t access = flags & O_ACCESS_MASK;
    uint32_t wanted = 0;
    Inode *inode;
    bool created = false;
    int status = vfs_lookup(base, path, &inode);

    if (status == -ENOENT && (flags & O_CREAT)) {
        status = vfs_create_path(base, path, mode, uid, gid, &inode);
        created = true;
    }
    if (status != 0)
        return status;

    if (access == O_RDONLY || access == O_RDWR)
        wanted |= PERMISSION_READ;
    if (access == O_WRONLY || access == O_RDWR)
        wanted |= PERMISSION_WRITE;

    // Whoever creates a file can always fill it in, even if its mode is read-only.
    if (!created)
        status = vfs_check_permission(inode, uid, gid, wanted);

    if (status == 0 && inode->type == INODE_DIRECTORY && (wanted & PERMISSION_WRITE))
        status = -EISDIR;

    if (status == 0 && (flags & O_TRUNC) && inode->type == INODE_FILE &&
        (wanted & PERMISSION_WRITE))
        status = inode->truncate();

    if (status != 0) {
        release_inode(inode);
        return status;
    }

    File *file = new InodeFile();

    file->inode = inode;
    file->flags = flags;
    *result = file;
    return 0;
}

void hold_file(File *file)
{
    file->refcount++;
}

void file_close(File *file)
{
    file->refcount--;
    if (file->refcount == 0) {
        file->close();
        delete file;
    }
}

int file_read(File *file, void *buffer, uint32_t count)
{
    if ((file->flags & O_ACCESS_MASK) == O_WRONLY)
        return -EBADF;

    // A folder holds entries, not text. Programs list one with readdir().
    if (file->inode != nullptr && file->inode->type == INODE_DIRECTORY)
        return -EISDIR;

    return file->read(buffer, count);
}

int file_write(File *file, const void *buffer, uint32_t count)
{
    if ((file->flags & O_ACCESS_MASK) == O_RDONLY)
        return -EBADF;
    return file->write(buffer, count);
}

int file_seek(File *file, int offset, int whence)
{
    int start;

    if (file->inode == nullptr)
        return -ESPIPE;

    if (whence == SEEK_SET)
        start = 0;
    else if (whence == SEEK_CUR)
        start = (int)file->offset;
    else if (whence == SEEK_END)
        start = (int)file->inode->size;
    else
        return -EINVAL;

    if (start + offset < 0)
        return -EINVAL;

    file->offset = start + offset;
    return (int)file->offset;
}
