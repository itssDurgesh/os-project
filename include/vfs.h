/* Virtual File System, the open-file table and pipes (owner: File Handling).
 * The VFS gives the kernel one interface over every filesystem. Each mounted
 * filesystem supplies its own table of inode functions. */

#ifndef VFS_H
#define VFS_H

#include <stdbool.h>
#include <stdint.h>

#include "abi.h"

#define MAX_OPEN_FILES 256      /* per process */

#define PERMISSION_READ  4
#define PERMISSION_WRITE 2
#define PERMISSION_EXEC  1

enum inode_type {
    INODE_FILE = TYPE_FILE,
    INODE_DIRECTORY = TYPE_DIRECTORY,
    INODE_DEVICE = TYPE_DEVICE,
};

struct inode;
struct pipe;
struct file;

struct inode_ops {
    int (*read)(struct inode *inode, void *buffer, uint32_t offset, uint32_t count);
    int (*write)(struct inode *inode, const void *buffer, uint32_t offset,
                 uint32_t count);
    int (*truncate)(struct inode *inode);
    int (*lookup)(struct inode *directory, const char *name, struct inode **result);
    int (*create)(struct inode *directory, const char *name, uint32_t mode,
                  uint32_t uid, uint32_t gid, struct inode **result);
    int (*mkdir)(struct inode *directory, const char *name, uint32_t mode,
                 uint32_t uid, uint32_t gid);
    int (*unlink)(struct inode *directory, const char *name);
    int (*rmdir)(struct inode *directory, const char *name);
    int (*readdir)(struct inode *directory, uint32_t *offset, struct dirent *entry);
    int (*sync)(struct inode *inode);       /* saves changed details */
    void (*release)(struct inode *inode);   /* the last reference has gone */
};

/* The in-memory copy of a file's details. One exists per open inode. */
struct inode {
    uint32_t number;
    enum inode_type type;
    uint32_t mode;              /* rwxrwxrwx */
    uint32_t uid;
    uint32_t gid;
    uint32_t links;
    uint32_t size;
    uint32_t refcount;
    const struct inode_ops *ops;
    void *private_data;         /* owned by the filesystem */
    struct inode *next;         /* the filesystem's list of cached inodes */
};

struct file_ops {
    int (*read)(struct file *file, void *buffer, uint32_t count);
    int (*write)(struct file *file, const void *buffer, uint32_t count);
    void (*close)(struct file *file);
};

/* Entry in the global open-file table. FDs point here, so a parent and child
 * share one position after fork(). */
struct file {
    uint32_t refcount;
    uint32_t flags;
    uint32_t offset;
    struct inode *inode;
    struct pipe *pipe;
    const struct file_ops *ops;
};

/* Mounting */
void vfs_init(void);
void vfs_set_root(struct inode *root);
void vfs_mount(const char *path, struct inode *root);
struct inode *vfs_root(void);

/* Inodes */
void inode_get(struct inode *inode);
void inode_put(struct inode *inode);

/* Paths. `base` is the directory that a relative path starts from. */
int vfs_lookup(struct inode *base, const char *path, struct inode **result);
int vfs_create_path(struct inode *base, const char *path, uint32_t mode,
                    uint32_t uid, uint32_t gid, struct inode **result);
int vfs_mkdir(struct inode *base, const char *path, uint32_t mode,
              uint32_t uid, uint32_t gid);
int vfs_unlink(struct inode *base, const char *path);
int vfs_rmdir(struct inode *base, const char *path);
int vfs_check_permission(const struct inode *inode, uint32_t uid, uint32_t gid,
                         uint32_t wanted);

/* Open files */
int file_open(struct inode *base, const char *path, uint32_t flags, uint32_t mode,
              uint32_t uid, uint32_t gid, struct file **result);
struct file *file_from_inode(struct inode *inode, uint32_t flags);
struct file *file_alloc(void);
void file_retain(struct file *file);
void file_close(struct file *file);
int file_read(struct file *file, void *buffer, uint32_t count);
int file_write(struct file *file, const void *buffer, uint32_t count);
int file_seek(struct file *file, int offset, int whence);

/* Console */
void console_set_echo(bool enabled);

/* Filesystems */
struct inode *ext2_mount(int partition);
struct inode *devfs_mount(void);

/* Pipes */
int pipe_create(struct file **read_end, struct file **write_end);

#endif
