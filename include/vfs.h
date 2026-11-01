// Virtual File System, open files and pipes (owner: File Handling).
//
// Inode and File are base classes. Each filesystem makes its own class from
// Inode and overrides the functions it supports, so the kernel can call
// inode->read() without caring which filesystem the file is on. The virtual
// functions are the "function pointer table" the PRD talks about.

#ifndef VFS_H
#define VFS_H

#include <stdbool.h>
#include <stdint.h>

#include "abi.h"

#define MAX_OPEN_FILES 256      // per process

#define PERMISSION_READ  4
#define PERMISSION_WRITE 2
#define PERMISSION_EXEC  1

enum InodeType {
    INODE_FILE = TYPE_FILE,
    INODE_DIRECTORY = TYPE_DIRECTORY,
    INODE_DEVICE = TYPE_DEVICE,
};

// The in-memory copy of a file's details. One exists per open file.
class Inode {
public:
    uint32_t number = 0;
    InodeType type = INODE_FILE;
    uint32_t mode = 0;          // rwxrwxrwx
    uint32_t uid = 0;
    uint32_t gid = 0;
    uint32_t links = 0;
    uint32_t size = 0;
    uint32_t refcount = 1;

    virtual ~Inode() {}

    // Files
    virtual int read(void *buffer, uint32_t offset, uint32_t count);
    virtual int write(const void *buffer, uint32_t offset, uint32_t count);
    virtual int truncate();

    // Folders
    virtual int lookup(const char *name, Inode **result);
    virtual int create(const char *name, uint32_t new_mode, uint32_t new_uid,
                       uint32_t new_gid, Inode **result);
    virtual int mkdir(const char *name, uint32_t new_mode, uint32_t new_uid,
                      uint32_t new_gid);
    virtual int unlink(const char *name);
    virtual int rmdir(const char *name);
    virtual int readdir(uint32_t *offset, struct dirent *entry);

    // Saves changed details, such as the mode after chmod.
    virtual int sync();

    // Called when the last user lets go of this inode.
    virtual void release();
};

// One entry in the global open-file table. FDs point here, so a parent and child
// share one file position after fork().
class File {
public:
    uint32_t refcount = 1;
    uint32_t flags = 0;
    uint32_t offset = 0;
    Inode *inode = nullptr;     // stays nullptr for a pipe

    virtual ~File() {}

    virtual int read(void *buffer, uint32_t count);
    virtual int write(const void *buffer, uint32_t count);
    virtual void close();
};

// Mounting
void vfs_set_root(Inode *root);
void vfs_mount(const char *path, Inode *root);
Inode *vfs_root();

// Inode reference counting
void hold_inode(Inode *inode);
void release_inode(Inode *inode);

// Paths. `base` is the folder a relative path starts from.
int vfs_lookup(Inode *base, const char *path, Inode **result);
int vfs_create_path(Inode *base, const char *path, uint32_t mode, uint32_t uid,
                    uint32_t gid, Inode **result);
int vfs_mkdir(Inode *base, const char *path, uint32_t mode, uint32_t uid, uint32_t gid);
int vfs_unlink(Inode *base, const char *path);
int vfs_rmdir(Inode *base, const char *path);
int vfs_check_permission(const Inode *inode, uint32_t uid, uint32_t gid, uint32_t wanted);

// Open files
int file_open(Inode *base, const char *path, uint32_t flags, uint32_t mode,
              uint32_t uid, uint32_t gid, File **result);
void hold_file(File *file);
void file_close(File *file);
int file_read(File *file, void *buffer, uint32_t count);
int file_write(File *file, const void *buffer, uint32_t count);
int file_seek(File *file, int offset, int whence);

// Console
void console_set_echo(bool enabled);

// Filesystems
Inode *ext2_mount(int partition);
void ext2_get_usage(uint32_t *total_kb, uint32_t *free_kb);
Inode *devfs_mount();
Inode *tmpfs_mount();
Inode *procfs_mount();

// Pipes
int pipe_create(File **read_end, File **write_end);

#endif
