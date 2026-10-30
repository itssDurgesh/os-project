// VFS: path lookup, the mount table and permission checks.

#include <stddef.h>

#include "console.h"
#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "vfs.h"

#define MAX_MOUNTS 4

struct Mount {
    Inode *covered;  // the directory hidden by this mount
    Inode *root;
};

static Inode *root_inode;
static Mount mounts[MAX_MOUNTS];
static int mount_count;

Inode *vfs_root()
{
    return root_inode;
}

void hold_inode(Inode *inode)
{
    inode->refcount++;
}

void release_inode(Inode *inode)
{
    inode->refcount--;
    if (inode->refcount == 0)
        inode->release();
}

static Inode *follow_mounts(Inode *inode)
{
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].covered == inode) {
            hold_inode(mounts[i].root);
            release_inode(inode);
            return mounts[i].root;
        }
    }
    return inode;
}

static const char *next_component(const char *path, char *name)
{
    int length = 0;

    while (*path == '/')
        path++;

    while (*path != '\0' && *path != '/') {
        if (length < MAX_FILE_NAME)
            name[length++] = *path;
        path++;
    }
    name[length] = '\0';
    return path;
}

// Walks a path. If `parent` is true, it stops at the last directory and leaves
// the final name in `last_name`.
static int walk(Inode *base, const char *path, Inode **result,
                bool parent, char *last_name)
{
    char name[MAX_FILE_NAME + 1];
    Inode *inode = (*path == '/' || base == nullptr) ? root_inode : base;

    hold_inode(inode);

    for (;;) {
        Inode *next;
        const char *rest = next_component(path, name);
        const char *after = rest;
        int status;

        if (name[0] == '\0') {
            if (parent) {
                release_inode(inode);
                return -EINVAL;
            }
            *result = inode;
            return 0;
        }

        while (*after == '/')
            after++;

        if (parent && *after == '\0') {
            copy_string(last_name, name, MAX_FILE_NAME + 1);
            *result = inode;
            return 0;
        }

        if (inode->type != INODE_DIRECTORY) {
            release_inode(inode);
            return -ENOTDIR;
        }

        // Searching a directory needs its execute permission.
        status = vfs_check_permission(inode, current_process->uid, current_process->gid,
                                      PERMISSION_EXEC);
        if (status == 0)
            status = inode->lookup(name, &next);
        release_inode(inode);
        if (status != 0)
            return status;

        inode = follow_mounts(next);
        path = rest;
    }
}

int vfs_lookup(Inode *base, const char *path, Inode **result)
{
    return walk(base, path, result, false, nullptr);
}

int vfs_create_path(Inode *base, const char *path, uint32_t mode,
                    uint32_t uid, uint32_t gid, Inode **result)
{
    char name[MAX_FILE_NAME + 1];
    Inode *directory;
    int status = walk(base, path, &directory, true, name);

    if (status != 0)
        return status;

    status = vfs_check_permission(directory, uid, gid, PERMISSION_WRITE);
    if (status == 0)
        status = directory->create(name, mode, uid, gid, result);

    release_inode(directory);
    return status;
}

int vfs_mkdir(Inode *base, const char *path, uint32_t mode,
              uint32_t uid, uint32_t gid)
{
    char name[MAX_FILE_NAME + 1];
    Inode *directory;
    int status = walk(base, path, &directory, true, name);

    if (status != 0)
        return status;

    status = vfs_check_permission(directory, uid, gid, PERMISSION_WRITE);
    if (status == 0)
        status = directory->mkdir(name, mode, uid, gid);

    release_inode(directory);
    return status;
}

int vfs_unlink(Inode *base, const char *path)
{
    char name[MAX_FILE_NAME + 1];
    Inode *directory;
    int status = walk(base, path, &directory, true, name);

    if (status != 0)
        return status;

    status = vfs_check_permission(directory, current_process->uid, current_process->gid,
                                  PERMISSION_WRITE);
    if (status == 0)
        status = directory->unlink(name);

    release_inode(directory);
    return status;
}

int vfs_rmdir(Inode *base, const char *path)
{
    char name[MAX_FILE_NAME + 1];
    Inode *directory;
    int status = walk(base, path, &directory, true, name);

    if (status != 0)
        return status;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        status = -EINVAL;
    else
        status = vfs_check_permission(directory, current_process->uid, current_process->gid,
                                      PERMISSION_WRITE);

    if (status == 0)
        status = directory->rmdir(name);

    release_inode(directory);
    return status;
}

// Unix permissions: use the owner, group or other bits, whichever applies.
// UID 0 is root and may do anything.
int vfs_check_permission(const Inode *inode, uint32_t uid, uint32_t gid,
                         uint32_t wanted)
{
    uint32_t bits;

    if (uid == 0)
        return 0;

    if (uid == inode->uid)
        bits = (inode->mode >> 6) & 7;
    else if (gid == inode->gid)
        bits = (inode->mode >> 3) & 7;
    else
        bits = inode->mode & 7;

    return ((bits & wanted) == wanted) ? 0 : -EACCES;
}

void vfs_mount(const char *path, Inode *root)
{
    Inode *covered;

    if (mount_count == MAX_MOUNTS)
        panic("the mount table is full");

    if (vfs_lookup(nullptr, path, &covered) != 0)
        panic("mount point %s doesn't exist", path);

    mounts[mount_count].covered = covered;  // keeps its reference
    mounts[mount_count].root = root;
    mount_count++;
}

void vfs_set_root(Inode *root)
{
    root_inode = root;
}

// The default answers. A filesystem overrides the ones it supports.
int Inode::read(void *, uint32_t, uint32_t) { return -EINVAL; }
int Inode::write(const void *, uint32_t, uint32_t) { return -EINVAL; }
int Inode::truncate() { return -EINVAL; }
int Inode::lookup(const char *, Inode **) { return -ENOTDIR; }
int Inode::create(const char *, uint32_t, uint32_t, uint32_t, Inode **) { return -EACCES; }
int Inode::mkdir(const char *, uint32_t, uint32_t, uint32_t) { return -EACCES; }
int Inode::unlink(const char *) { return -EACCES; }
int Inode::rmdir(const char *) { return -EACCES; }
int Inode::readdir(uint32_t *, struct dirent *) { return -ENOTDIR; }
int Inode::sync() { return -EACCES; }
void Inode::release() {}
