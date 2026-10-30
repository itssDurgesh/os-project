// File system calls. Each one checks the program's pointers and file descriptor
// before doing anything else.

#include <stddef.h>

#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "syscall.h"
#include "vfs.h"

static File *get_file(int fd)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES)
        return nullptr;
    return current_process->files[fd];
}

static int find_free_fd()
{
    for (int fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (current_process->files[fd] == nullptr)
            return fd;
    }
    return -EMFILE;
}

int sys_open(Registers *r)
{
    char path[MAX_PATH];
    File *file;
    int fd = find_free_fd();
    int status;

    if (fd < 0)
        return fd;

    status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);
    if (status != 0)
        return status;

    status = file_open(current_process->cwd, path, r->ecx, r->edx & 0777,
                       current_process->uid, current_process->gid, &file);
    if (status != 0)
        return status;

    current_process->files[fd] = file;
    return fd;
}

int sys_close(Registers *r)
{
    int fd = (int)r->ebx;
    File *file = get_file(fd);

    if (file == nullptr)
        return -EBADF;

    current_process->files[fd] = nullptr;
    file_close(file);
    return 0;
}

int sys_read(Registers *r)
{
    File *file = get_file((int)r->ebx);

    if (file == nullptr)
        return -EBADF;
    if (!user_range_valid((void *)r->ecx, r->edx, true))
        return -EFAULT;

    return file_read(file, (void *)r->ecx, r->edx);
}

int sys_write(Registers *r)
{
    File *file = get_file((int)r->ebx);

    if (file == nullptr)
        return -EBADF;
    if (!user_range_valid((void *)r->ecx, r->edx, false))
        return -EFAULT;

    return file_write(file, (const void *)r->ecx, r->edx);
}

int sys_seek(Registers *r)
{
    File *file = get_file((int)r->ebx);

    if (file == nullptr)
        return -EBADF;

    return file_seek(file, (int)r->ecx, (int)r->edx);
}

int sys_stat(Registers *r)
{
    char path[MAX_PATH];
    struct stat *output = (struct stat *)r->ecx;
    Inode *inode;
    int status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);

    if (status != 0)
        return status;
    if (!user_range_valid(output, sizeof(*output), true))
        return -EFAULT;

    status = vfs_lookup(current_process->cwd, path, &inode);
    if (status != 0)
        return status;

    output->inode = inode->number;
    output->type = inode->type;
    output->mode = inode->mode;
    output->uid = inode->uid;
    output->gid = inode->gid;
    output->links = inode->links;
    output->size = inode->size;

    release_inode(inode);
    return 0;
}

int sys_unlink(Registers *r)
{
    char path[MAX_PATH];
    int status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);

    return (status != 0) ? status : vfs_unlink(current_process->cwd, path);
}

int sys_mkdir(Registers *r)
{
    char path[MAX_PATH];
    int status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);

    if (status != 0)
        return status;

    return vfs_mkdir(current_process->cwd, path, r->ecx & 0777, current_process->uid, current_process->gid);
}

int sys_rmdir(Registers *r)
{
    char path[MAX_PATH];
    int status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);

    return (status != 0) ? status : vfs_rmdir(current_process->cwd, path);
}

// Returns 1 when it fills in an entry, and 0 at the end of the directory.
int sys_readdir(Registers *r)
{
    File *file = get_file((int)r->ebx);
    struct dirent *entry = (struct dirent *)r->ecx;

    if (file == nullptr || file->inode == nullptr)
        return -EBADF;
    if (file->inode->type != INODE_DIRECTORY)
        return -ENOTDIR;
    if (!user_range_valid(entry, sizeof(*entry), true))
        return -EFAULT;

    return file->inode->readdir(&file->offset, entry);
}

// Rewrites the path with every "." and ".." worked out, for getcwd().
static void update_cwd_path(const char *path)
{
    char result[MAX_PATH];
    uint32_t length = 0;
    const char *from = path;

    if (path[0] != '/') {
        copy_string(result, current_process->cwd_path, MAX_PATH);
        length = strlen(result);
        if (length == 1)
            length = 0;  // "/" becomes the empty prefix
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
    copy_string(current_process->cwd_path, result, MAX_PATH);
}

int sys_chdir(Registers *r)
{
    char path[MAX_PATH];
    Inode *inode;
    int status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);

    if (status != 0)
        return status;

    status = vfs_lookup(current_process->cwd, path, &inode);
    if (status != 0)
        return status;

    if (inode->type != INODE_DIRECTORY) {
        release_inode(inode);
        return -ENOTDIR;
    }

    release_inode(current_process->cwd);
    current_process->cwd = inode;
    update_cwd_path(path);
    return 0;
}

int sys_getcwd(Registers *r)
{
    char *output = (char *)r->ebx;
    uint32_t size = r->ecx;
    uint32_t length = strlen(current_process->cwd_path) + 1;

    if (!user_range_valid(output, size, true))
        return -EFAULT;
    if (length > size)
        return -EINVAL;

    memcpy(output, current_process->cwd_path, length);
    return 0;
}

int sys_chmod(Registers *r)
{
    char path[MAX_PATH];
    Inode *inode;
    int status = copy_string_from_user(path, (const char *)r->ebx, MAX_PATH);

    if (status != 0)
        return status;

    status = vfs_lookup(current_process->cwd, path, &inode);
    if (status != 0)
        return status;

    if (current_process->uid != 0 && (uint32_t)current_process->uid != inode->uid) {
        status = -EPERM;
    } else {
        inode->mode = r->ecx & 0777;
        status = inode->sync();
    }

    release_inode(inode);
    return status;
}

// login turns echo off while the password is typed.
int sys_set_echo(Registers *r)
{
    if (current_process->uid != 0)
        return -EPERM;

    console_set_echo(r->ebx != 0);
    return 0;
}

int sys_dup2(Registers *r)
{
    int old_fd = (int)r->ebx;
    int new_fd = (int)r->ecx;
    File *file = get_file(old_fd);

    if (file == nullptr || new_fd < 0 || new_fd >= MAX_OPEN_FILES)
        return -EBADF;

    if (old_fd == new_fd)
        return new_fd;

    if (current_process->files[new_fd] != nullptr)
        file_close(current_process->files[new_fd]);

    hold_file(file);
    current_process->files[new_fd] = file;
    return new_fd;
}
