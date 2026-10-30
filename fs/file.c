/* The global open-file table. Each entry holds a position, access mode and
 * reference count, and points at an inode or a pipe. */

#include <stddef.h>

#include "errno.h"
#include "memory.h"
#include "vfs.h"

static int inode_file_read(struct file *file, void *buffer, uint32_t count)
{
    int result = file->inode->ops->read(file->inode, buffer, file->offset, count);

    if (result > 0)
        file->offset += result;
    return result;
}

static int inode_file_write(struct file *file, const void *buffer, uint32_t count)
{
    int result;

    if (file->flags & O_APPEND)
        file->offset = file->inode->size;

    result = file->inode->ops->write(file->inode, buffer, file->offset, count);
    if (result > 0)
        file->offset += result;
    return result;
}

static void inode_file_close(struct file *file)
{
    inode_put(file->inode);
}

static const struct file_ops inode_file_ops = {
    .read = inode_file_read,
    .write = inode_file_write,
    .close = inode_file_close,
};

struct file *file_alloc(void)
{
    struct file *file = kzalloc(sizeof(*file));

    if (file != NULL)
        file->refcount = 1;
    return file;
}

/* Takes over the caller's reference to the inode. */
struct file *file_from_inode(struct inode *inode, uint32_t flags)
{
    struct file *file = file_alloc();

    if (file != NULL) {
        file->inode = inode;
        file->flags = flags;
        file->ops = &inode_file_ops;
    }
    return file;
}

int file_open(struct inode *base, const char *path, uint32_t flags, uint32_t mode,
              uint32_t uid, uint32_t gid, struct file **result)
{
    uint32_t access = flags & O_ACCESS_MASK;
    uint32_t wanted = 0;
    struct inode *inode;
    int status = vfs_lookup(base, path, &inode);

    if (status == -ENOENT && (flags & O_CREAT))
        status = vfs_create_path(base, path, mode, uid, gid, &inode);
    if (status != 0)
        return status;

    if (access == O_RDONLY || access == O_RDWR)
        wanted |= PERMISSION_READ;
    if (access == O_WRONLY || access == O_RDWR)
        wanted |= PERMISSION_WRITE;

    status = vfs_check_permission(inode, uid, gid, wanted);
    if (status == 0 && inode->type == INODE_DIRECTORY && (wanted & PERMISSION_WRITE))
        status = -EISDIR;

    if (status == 0 && (flags & O_TRUNC) && inode->type == INODE_FILE &&
        (wanted & PERMISSION_WRITE))
        status = inode->ops->truncate(inode);

    if (status != 0) {
        inode_put(inode);
        return status;
    }

    *result = file_from_inode(inode, flags);
    if (*result == NULL) {
        inode_put(inode);
        return -ENOMEM;
    }
    return 0;
}

void file_retain(struct file *file)
{
    file->refcount++;
}

void file_close(struct file *file)
{
    if (--file->refcount == 0) {
        file->ops->close(file);
        kfree(file);
    }
}

int file_read(struct file *file, void *buffer, uint32_t count)
{
    if ((file->flags & O_ACCESS_MASK) == O_WRONLY)
        return -EBADF;
    return file->ops->read(file, buffer, count);
}

int file_write(struct file *file, const void *buffer, uint32_t count)
{
    if ((file->flags & O_ACCESS_MASK) == O_RDONLY)
        return -EBADF;
    return file->ops->write(file, buffer, count);
}

int file_seek(struct file *file, int offset, int whence)
{
    int base;

    if (file->inode == NULL)
        return -ESPIPE;

    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = (int)file->offset;
        break;
    case SEEK_END:
        base = (int)file->inode->size;
        break;
    default:
        return -EINVAL;
    }

    if (base + offset < 0)
        return -EINVAL;

    file->offset = base + offset;
    return (int)file->offset;
}
