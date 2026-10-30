/* File system calls. Each one checks the program's pointers and file descriptor
 * before doing anything else. */

#include <stddef.h>

#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "syscall.h"
#include "vfs.h"

static struct file *get_file(int fd)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return NULL;
    return current->files[fd];
}

static int find_free_fd(void)
{
    for (int fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (current->files[fd] == NULL)
            return fd;
    }
    return -EMFILE;
}

int sys_open(struct regs *r)
{
    char path[MAX_PATH];
    struct file *file;
    int fd = find_free_fd();
    int status;

    if (fd < 0)
        return fd;

    status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);
    if (status != 0)
        return status;

    status = file_open(current->cwd, path, r->ecx, r->edx & 0777,
                       current->uid, current->gid, &file);
    if (status != 0)
        return status;

    current->files[fd] = file;
    return fd;
}

int sys_close(struct regs *r)
{
    int fd = (int)r->ebx;
    struct file *file = get_file(fd);

    if (file == NULL)
        return -EBADF;

    current->files[fd] = NULL;
    file_close(file);
    return 0;
}

int sys_read(struct regs *r)
{
    struct file *file = get_file((int)r->ebx);

    if (file == NULL)
        return -EBADF;
    if (!user_range_valid((void *)r->ecx, r->edx, true))
        return -EFAULT;

    return file_read(file, (void *)r->ecx, r->edx);
}

int sys_write(struct regs *r)
{
    struct file *file = get_file((int)r->ebx);

    if (file == NULL)
        return -EBADF;
    if (!user_range_valid((void *)r->ecx, r->edx, false))
        return -EFAULT;

    return file_write(file, (const void *)r->ecx, r->edx);
}

int sys_seek(struct regs *r)
{
    struct file *file = get_file((int)r->ebx);

    if (file == NULL)
        return -EBADF;

    return file_seek(file, (int)r->ecx, (int)r->edx);
}

int sys_stat(struct regs *r)
{
    char path[MAX_PATH];
    struct stat *output = (struct stat *)r->ecx;
    struct inode *inode;
    int status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);

    if (status != 0)
        return status;
    if (!user_range_valid(output, sizeof(*output), true))
        return -EFAULT;

    status = vfs_lookup(current->cwd, path, &inode);
    if (status != 0)
        return status;

    output->inode = inode->number;
    output->type = inode->type;
    output->mode = inode->mode;
    output->uid = inode->uid;
    output->gid = inode->gid;
    output->links = inode->links;
    output->size = inode->size;

    inode_put(inode);
    return 0;
}

int sys_unlink(struct regs *r)
{
    char path[MAX_PATH];
    int status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);

    return (status != 0) ? status : vfs_unlink(current->cwd, path);
}

int sys_mkdir(struct regs *r)
{
    char path[MAX_PATH];
    int status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);

    if (status != 0)
        return status;

    return vfs_mkdir(current->cwd, path, r->ecx & 0777, current->uid, current->gid);
}

int sys_rmdir(struct regs *r)
{
    char path[MAX_PATH];
    int status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);

    return (status != 0) ? status : vfs_rmdir(current->cwd, path);
}

/* Returns 1 when it fills in an entry, and 0 at the end of the directory. */
int sys_readdir(struct regs *r)
{
    struct file *file = get_file((int)r->ebx);
    struct dirent *entry = (struct dirent *)r->ecx;

    if (file == NULL || file->inode == NULL)
        return -EBADF;
    if (file->inode->type != INODE_DIRECTORY)
        return -ENOTDIR;
    if (!user_range_valid(entry, sizeof(*entry), true))
        return -EFAULT;

    return file->inode->ops->readdir(file->inode, &file->offset, entry);
}

/* Rewrites the path with every "." and ".." worked out, for getcwd(). */
static void update_cwd_path(const char *path)
{
    char result[MAX_PATH];
    uint32_t length = 0;
    const char *from = path;

    if (path[0] != '/') {
        strlcpy(result, current->cwd_path, MAX_PATH);
        length = strlen(result);
        if (length == 1)
            length = 0;         /* "/" becomes the empty prefix */
    }

    while (*from != '\0') {
        const char *start;
        uint32_t size;

        while (*from == '/')
            from++;

        start = from;
        while (*from != '\0' && *from != '/')
            from++;
        size = from - start;

        if (size == 0 || (size == 1 && start[0] == '.'))
            continue;

        if (size == 2 && start[0] == '.' && start[1] == '.') {
            while (length > 0 && result[--length] != '/')
                ;
            continue;
        }

        if (length + size + 2 < MAX_PATH) {
            result[length++] = '/';
            memcpy(result + length, start, size);
            length += size;
        }
    }

    if (length == 0)
        result[length++] = '/';
    result[length] = '\0';
    strlcpy(current->cwd_path, result, MAX_PATH);
}

int sys_chdir(struct regs *r)
{
    char path[MAX_PATH];
    struct inode *inode;
    int status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);

    if (status != 0)
        return status;

    status = vfs_lookup(current->cwd, path, &inode);
    if (status != 0)
        return status;

    if (inode->type != INODE_DIRECTORY) {
        inode_put(inode);
        return -ENOTDIR;
    }

    inode_put(current->cwd);
    current->cwd = inode;
    update_cwd_path(path);
    return 0;
}

int sys_getcwd(struct regs *r)
{
    char *output = (char *)r->ebx;
    uint32_t size = r->ecx;
    uint32_t length = strlen(current->cwd_path) + 1;

    if (!user_range_valid(output, size, true))
        return -EFAULT;
    if (length > size)
        return -EINVAL;

    memcpy(output, current->cwd_path, length);
    return 0;
}

int sys_dup2(struct regs *r)
{
    int old_fd = (int)r->ebx;
    int new_fd = (int)r->ecx;
    struct file *file = get_file(old_fd);

    if (file == NULL || new_fd < 0 || new_fd >= MAX_OPEN_FILES)
        return -EBADF;

    if (old_fd == new_fd)
        return new_fd;

    if (current->files[new_fd] != NULL)
        file_close(current->files[new_fd]);

    file_retain(file);
    current->files[new_fd] = file;
    return new_fd;
}
