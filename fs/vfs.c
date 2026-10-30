/* VFS: path lookup, the mount table and permission checks. */

#include <stddef.h>

#include "console.h"
#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "vfs.h"

#define MAX_MOUNTS 4

struct mount {
    struct inode *covered;      /* the directory hidden by this mount */
    struct inode *root;
};

static struct inode *root_inode;
static struct mount mounts[MAX_MOUNTS];
static int mount_count;

struct inode *vfs_root(void)
{
    return root_inode;
}

void inode_get(struct inode *inode)
{
    inode->refcount++;
}

void inode_put(struct inode *inode)
{
    if (--inode->refcount == 0 && inode->ops->release != NULL)
        inode->ops->release(inode);
}

static struct inode *follow_mounts(struct inode *inode)
{
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].covered == inode) {
            inode_get(mounts[i].root);
            inode_put(inode);
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

/* Walks a path. If `parent` is true, it stops at the last directory and leaves
 * the final name in `last_name`. */
static int walk(struct inode *base, const char *path, struct inode **result,
                bool parent, char *last_name)
{
    char name[MAX_FILE_NAME + 1];
    struct inode *inode = (*path == '/' || base == NULL) ? root_inode : base;

    inode_get(inode);

    for (;;) {
        struct inode *next;
        const char *rest = next_component(path, name);
        const char *after = rest;
        int status;

        if (name[0] == '\0') {
            if (parent) {
                inode_put(inode);
                return -EINVAL;
            }
            *result = inode;
            return 0;
        }

        while (*after == '/')
            after++;

        if (parent && *after == '\0') {
            strlcpy(last_name, name, MAX_FILE_NAME + 1);
            *result = inode;
            return 0;
        }

        if (inode->type != INODE_DIRECTORY) {
            inode_put(inode);
            return -ENOTDIR;
        }

        status = inode->ops->lookup(inode, name, &next);
        inode_put(inode);
        if (status != 0)
            return status;

        inode = follow_mounts(next);
        path = rest;
    }
}

int vfs_lookup(struct inode *base, const char *path, struct inode **result)
{
    return walk(base, path, result, false, NULL);
}

int vfs_create_path(struct inode *base, const char *path, uint32_t mode,
                    uint32_t uid, uint32_t gid, struct inode **result)
{
    char name[MAX_FILE_NAME + 1];
    struct inode *directory;
    int status = walk(base, path, &directory, true, name);

    if (status != 0)
        return status;

    if (directory->ops->create == NULL)
        status = -EACCES;
    else if ((status = vfs_check_permission(directory, uid, gid, PERMISSION_WRITE)) == 0)
        status = directory->ops->create(directory, name, mode, uid, gid, result);

    inode_put(directory);
    return status;
}

int vfs_mkdir(struct inode *base, const char *path, uint32_t mode,
              uint32_t uid, uint32_t gid)
{
    char name[MAX_FILE_NAME + 1];
    struct inode *directory;
    int status = walk(base, path, &directory, true, name);

    if (status != 0)
        return status;

    if (directory->ops->mkdir == NULL)
        status = -EACCES;
    else if ((status = vfs_check_permission(directory, uid, gid, PERMISSION_WRITE)) == 0)
        status = directory->ops->mkdir(directory, name, mode, uid, gid);

    inode_put(directory);
    return status;
}

int vfs_unlink(struct inode *base, const char *path)
{
    char name[MAX_FILE_NAME + 1];
    struct inode *directory;
    int status = walk(base, path, &directory, true, name);

    if (status != 0)
        return status;

    status = (directory->ops->unlink != NULL)
                 ? directory->ops->unlink(directory, name) : -EACCES;
    inode_put(directory);
    return status;
}

int vfs_rmdir(struct inode *base, const char *path)
{
    char name[MAX_FILE_NAME + 1];
    struct inode *directory;
    int status = walk(base, path, &directory, true, name);

    if (status != 0)
        return status;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        status = -EINVAL;
    else
        status = (directory->ops->rmdir != NULL)
                     ? directory->ops->rmdir(directory, name) : -EACCES;

    inode_put(directory);
    return status;
}

/* Unix permissions: use the owner, group or other bits, whichever applies.
 * UID 0 is root and may do anything. */
int vfs_check_permission(const struct inode *inode, uint32_t uid, uint32_t gid,
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

void vfs_mount(const char *path, struct inode *root)
{
    struct inode *covered;

    if (mount_count == MAX_MOUNTS)
        panic("the mount table is full");

    if (vfs_lookup(NULL, path, &covered) != 0)
        panic("mount point %s doesn't exist", path);

    mounts[mount_count].covered = covered;      /* keeps its reference */
    mounts[mount_count].root = root;
    mount_count++;
}

void vfs_init(void)
{
    root_inode = NULL;
    mount_count = 0;
}

void vfs_set_root(struct inode *root)
{
    root_inode = root;
}
