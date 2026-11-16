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

// Saves the full path in the File, so lsof can show which file it is. A relative
// path gets the current folder in front.
static void remember_file_name(File *file, const char *path)
{
    char full_path[MAX_PATH];
    uint32_t length = 0;

    if (path[0] != '/') {
        copy_string(full_path, current_process->cwd_path, MAX_PATH);
        length = strlen(full_path);
        // "/" already ends in a slash. Any other folder needs one added.
        if (length > 1 && length + 1 < MAX_PATH)
            full_path[length++] = '/';
    }

    copy_string(full_path + length, path, MAX_PATH - length);
    copy_string(file->name, full_path, FILE_NAME_LENGTH);
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

    remember_file_name(file, path);
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

// Copies through a kernel buffer. If the filesystem wrote straight into user
// memory it could page fault while holding its lock, and the fault might need the
// same lock to load the page. See the rule in memory/virtual_memory.cpp.
int sys_read(Registers *r)
{
    File *file = get_file((int)r->ebx);
    char *user_buffer = (char *)r->ecx;
    uint32_t count = r->edx;
    uint32_t done = 0;

    if (file == nullptr)
        return -EBADF;
    if (!user_range_valid(user_buffer, count, true))
        return -EFAULT;

    char *kernel_buffer = (char *)kmalloc(PAGE_SIZE);

    if (kernel_buffer == nullptr)
        return -ENOMEM;

    while (done < count) {
        uint32_t chunk = count - done;

        if (chunk > PAGE_SIZE)
            chunk = PAGE_SIZE;

        int result = file_read(file, kernel_buffer, chunk);

        if (result < 0 && done == 0) {
            kfree(kernel_buffer);
            return result;
        }
        if (result <= 0)
            break;

        memcpy(user_buffer + done, kernel_buffer, result);
        done += result;

        if ((uint32_t)result < chunk)
            break;              // a short read: nothing more for now
    }

    kfree(kernel_buffer);
    return (int)done;
}

int sys_write(Registers *r)
{
    File *file = get_file((int)r->ebx);
    const char *user_buffer = (const char *)r->ecx;
    uint32_t count = r->edx;
    uint32_t done = 0;

    if (file == nullptr)
        return -EBADF;
    if (!user_range_valid(user_buffer, count, false))
        return -EFAULT;

    char *kernel_buffer = (char *)kmalloc(PAGE_SIZE);

    if (kernel_buffer == nullptr)
        return -ENOMEM;

    while (done < count) {
        uint32_t chunk = count - done;

        if (chunk > PAGE_SIZE)
            chunk = PAGE_SIZE;

        memcpy(kernel_buffer, user_buffer + done, chunk);

        int result = file_write(file, kernel_buffer, chunk);

        if (result < 0 && done == 0) {
            kfree(kernel_buffer);
            return result;
        }
        if (result <= 0)
            break;

        done += result;
    }

    kfree(kernel_buffer);
    return (int)done;
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

    struct dirent *kernel_entry = (struct dirent *)kmalloc(sizeof(struct dirent));

    if (kernel_entry == nullptr)
        return -ENOMEM;

    int result = file->inode->readdir(&file->offset, kernel_entry);

    if (result > 0)
        memcpy(entry, kernel_entry, sizeof(*entry));

    kfree(kernel_entry);
    return result;
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

// SYS_PROCFILE, for lsof: ebx = pid (0 means me), ecx = the first fd to look at,
// edx = struct fileinfo to fill in. Returns the fd it found, or -ENOENT when the
// process has no more open files.
int sys_procfile(Registers *r)
{
    int pid = (int)r->ebx;
    int first_fd = (int)r->ecx;
    struct fileinfo *output = (struct fileinfo *)r->edx;
    struct fileinfo info;
    Process *process;

    if (!user_range_valid(output, sizeof(*output), true))
        return -EFAULT;

    process = (pid == 0) ? current_process : find_process(pid);
    if (process == nullptr)
        return -ESRCH;
    if (process->files == nullptr || first_fd < 0)
        return -ENOENT;

    for (int fd = first_fd; fd < MAX_OPEN_FILES; fd++) {
        File *file = process->files[fd];

        if (file == nullptr)
            continue;

        memset(&info, 0, sizeof(info));
        info.fd = fd;
        info.flags = file->flags;
        info.offset = file->offset;
        info.shared_by = file->refcount;
        copy_string(info.name, file->name, FILE_NAME_LENGTH);

        if (file->inode == nullptr) {
            info.kind = FILE_KIND_PIPE;
        } else {
            info.kind = file->inode->type;   // the same numbers as FILE_KIND_
            info.size = file->inode->size;
            info.inode = file->inode->number;
        }

        memcpy(output, &info, sizeof(info));
        return fd;
    }
    return -ENOENT;
}
