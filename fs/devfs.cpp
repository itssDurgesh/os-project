/* devfs: the /dev folder. Its files are devices, held in memory, not on disk.
 *   /dev/console  line-at-a-time keyboard input and screen output
 *   /dev/input    raw keyboard events, one character at a time
 *   /dev/null     discards writes and always reads as empty */

#include <stddef.h>

#include "console.h"
#include "errno.h"
#include "input.h"
#include "kstring.h"
#include "vfs.h"

#define KEY_BACKSPACE '\b'
#define KEY_DELETE    0x7F
#define KEY_END_OF_FILE 0x04    /* Ctrl+D */

#define DEVICE_MODE 0666
#define DEVFS_MODE  0755

#define LINE_LENGTH 256

static char line[LINE_LENGTH];
static uint32_t line_length;
static uint32_t line_position;
static bool echo_enabled = true;

void console_set_echo(bool enabled)
{
    echo_enabled = enabled;
}

/* Reads a whole line with echo and backspace. Returns false on Ctrl+D. */
static bool read_line(void)
{
    line_length = 0;
    line_position = 0;

    for (;;) {
        char c = input_get_char();

        if (c == KEY_END_OF_FILE && line_length == 0)
            return false;

        if (c == KEY_BACKSPACE || c == KEY_DELETE) {
            if (line_length > 0) {
                line_length--;
                if (echo_enabled)
                    kprintf("\b");
            }
            continue;
        }

        if (c == '\r')
            c = '\n';

        if (c == '\n' || line_length < LINE_LENGTH - 1) {
            line[line_length++] = c;
            if (echo_enabled)
                kprintf("%c", c);
        }

        if (c == '\n')
            return true;
    }
}

static int console_read(struct inode *inode, void *buffer, uint32_t offset, uint32_t count)
{
    char *output = (char *)buffer;
    uint32_t done = 0;

    (void)inode;
    (void)offset;

    if (line_position == line_length && !read_line())
        return 0;

    while (done < count && line_position < line_length)
        output[done++] = line[line_position++];

    return (int)done;
}

static int console_write(struct inode *inode, const void *buffer, uint32_t offset,
                         uint32_t count)
{
    const char *input = (const char *)buffer;

    (void)inode;
    (void)offset;

    for (uint32_t i = 0; i < count; i++)
        kprintf("%c", input[i]);
    return (int)count;
}

static int raw_input_read(struct inode *inode, void *buffer, uint32_t offset, uint32_t count)
{
    (void)inode;
    (void)offset;

    if (count == 0)
        return 0;

    *(char *)buffer = input_get_char();
    return 1;
}

static int null_read(struct inode *inode, void *buffer, uint32_t offset, uint32_t count)
{
    (void)inode;
    (void)buffer;
    (void)offset;
    (void)count;
    return 0;
}

static int null_write(struct inode *inode, const void *buffer, uint32_t offset, uint32_t count)
{
    (void)inode;
    (void)buffer;
    (void)offset;
    return (int)count;
}

static int device_truncate(struct inode *inode)
{
    (void)inode;
    return 0;
}

static const struct inode_ops console_ops = {
    .read = console_read,
    .write = console_write,
    .truncate = device_truncate,
};

static const struct inode_ops input_ops = {
    .read = raw_input_read,
    .write = null_write,
    .truncate = device_truncate,
};

static const struct inode_ops null_ops = {
    .read = null_read,
    .write = null_write,
    .truncate = device_truncate,
};

#define DEVICE_COUNT 3

static struct {
    const char *name;
    struct inode inode;
} devices[DEVICE_COUNT] = {
    { "console", { .number = 1, .ops = &console_ops } },
    { "input",   { .number = 2, .ops = &input_ops } },
    { "null",    { .number = 3, .ops = &null_ops } },
};

static int devfs_lookup(struct inode *directory, const char *name, struct inode **result)
{
    if (strcmp(name, ".") == 0) {
        hold_inode(directory);
        *result = directory;
        return 0;
    }

    if (strcmp(name, "..") == 0) {
        hold_inode(vfs_root());
        *result = vfs_root();
        return 0;
    }

    for (int i = 0; i < DEVICE_COUNT; i++) {
        if (strcmp(name, devices[i].name) == 0) {
            hold_inode(&devices[i].inode);
            *result = &devices[i].inode;
            return 0;
        }
    }
    return -ENOENT;
}

static int devfs_readdir(struct inode *directory, uint32_t *offset, struct dirent *entry)
{
    (void)directory;

    if (*offset >= DEVICE_COUNT)
        return 0;

    entry->inode = devices[*offset].inode.number;
    entry->type = INODE_DEVICE;
    copy_string(entry->name, devices[*offset].name, sizeof(entry->name));
    (*offset)++;
    return 1;
}

static const struct inode_ops devfs_ops = {
    .lookup = devfs_lookup,
    .readdir = devfs_readdir,
};

static struct inode devfs_root = {
    .number = 100,
    .type = INODE_DIRECTORY,
    .mode = DEVFS_MODE,
    .links = 2,
    .refcount = 1,
    .ops = &devfs_ops,
};

struct inode *devfs_mount(void)
{
    for (int i = 0; i < DEVICE_COUNT; i++) {
        devices[i].inode.type = INODE_DEVICE;
        devices[i].inode.mode = DEVICE_MODE;
        devices[i].inode.links = 1;
        devices[i].inode.refcount = 1;
    }
    return &devfs_root;
}
