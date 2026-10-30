// devfs: the /dev folder. These files are devices, so they live in memory and
// not on the disk.
//   /dev/console  keyboard input one line at a time, and output to the screen
//   /dev/input    raw keyboard input, one character at a time
//   /dev/null     throws away writes and is always empty when read

#include "console.h"
#include "errno.h"
#include "input.h"
#include "kstring.h"
#include "vfs.h"

#define KEY_BACKSPACE '\b'
#define KEY_DELETE    0x7F
#define KEY_END_OF_FILE 0x04    // Ctrl+D

#define DEVICE_MODE 0666
#define DEVFS_MODE  0755

#define LINE_LENGTH 256
#define DEVICE_COUNT 4

static char line[LINE_LENGTH];
static uint32_t line_length;
static uint32_t line_position;
static bool echo_enabled = true;

void console_set_echo(bool enabled)
{
    echo_enabled = enabled;
}

// Reads a whole line, with echo and backspace. Returns 1 for a line, 0 for
// Ctrl+D, or -EINTR if a signal arrived.
static int read_line()
{
    line_length = 0;
    line_position = 0;

    for (;;) {
        char c;

        if (!input_get_char(&c)) {
            line_length = 0;
            return -EINTR;
        }

        if (c == KEY_END_OF_FILE && line_length == 0)
            return 0;

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
            line[line_length] = c;
            line_length++;
            if (echo_enabled)
                kprintf("%c", c);
        }

        if (c == '\n')
            return 1;
    }
}

class ConsoleDevice : public Inode {
public:
    int read(void *buffer, uint32_t offset, uint32_t count) override;
    int write(const void *buffer, uint32_t offset, uint32_t count) override;
    int truncate() override;
};

int ConsoleDevice::read(void *buffer, uint32_t, uint32_t count)
{
    char *output = (char *)buffer;
    uint32_t done = 0;

    if (line_position == line_length) {
        int status = read_line();

        if (status <= 0)
            return status;
    }

    while (done < count && line_position < line_length) {
        output[done] = line[line_position];
        done++;
        line_position++;
    }
    return (int)done;
}

int ConsoleDevice::write(const void *buffer, uint32_t, uint32_t count)
{
    const char *input = (const char *)buffer;

    for (uint32_t i = 0; i < count; i++)
        kprintf("%c", input[i]);
    return (int)count;
}

int ConsoleDevice::truncate()
{
    return 0;
}

class RawInputDevice : public Inode {
public:
    int read(void *buffer, uint32_t offset, uint32_t count) override;
    int write(const void *buffer, uint32_t offset, uint32_t count) override;
    int truncate() override;
};

int RawInputDevice::read(void *buffer, uint32_t, uint32_t count)
{
    if (count == 0)
        return 0;

    if (!input_get_char((char *)buffer))
        return -EINTR;
    return 1;
}

int RawInputDevice::write(const void *, uint32_t, uint32_t count)
{
    return (int)count;
}

int RawInputDevice::truncate()
{
    return 0;
}

class NullDevice : public Inode {
public:
    int read(void *buffer, uint32_t offset, uint32_t count) override;
    int write(const void *buffer, uint32_t offset, uint32_t count) override;
    int truncate() override;
};

int NullDevice::read(void *, uint32_t, uint32_t)
{
    return 0;
}

int NullDevice::write(const void *, uint32_t, uint32_t count)
{
    return (int)count;
}

int NullDevice::truncate()
{
    return 0;
}

// Each read gives one MouseEvent. Programs can't write to the mouse.
class MouseDevice : public Inode {
public:
    int read(void *buffer, uint32_t offset, uint32_t count) override;
    int write(const void *buffer, uint32_t offset, uint32_t count) override;
    int truncate() override;
};

int MouseDevice::read(void *buffer, uint32_t, uint32_t count)
{
    if (count < sizeof(MouseEvent))
        return -EINVAL;

    if (!mouse_get_event((MouseEvent *)buffer))
        return -EINTR;
    return sizeof(MouseEvent);
}

int MouseDevice::write(const void *, uint32_t, uint32_t)
{
    return -EINVAL;
}

int MouseDevice::truncate()
{
    return 0;
}

struct DeviceEntry {
    const char *name;
    Inode *inode;
};

static DeviceEntry devices[DEVICE_COUNT];

class DevFolder : public Inode {
public:
    int lookup(const char *name, Inode **result) override;
    int readdir(uint32_t *offset, struct dirent *entry) override;
};

int DevFolder::lookup(const char *name, Inode **result)
{
    if (strcmp(name, ".") == 0) {
        hold_inode(this);
        *result = this;
        return 0;
    }

    if (strcmp(name, "..") == 0) {
        hold_inode(vfs_root());
        *result = vfs_root();
        return 0;
    }

    for (int i = 0; i < DEVICE_COUNT; i++) {
        if (strcmp(name, devices[i].name) == 0) {
            hold_inode(devices[i].inode);
            *result = devices[i].inode;
            return 0;
        }
    }
    return -ENOENT;
}

int DevFolder::readdir(uint32_t *offset, struct dirent *entry)
{
    if (*offset >= DEVICE_COUNT)
        return 0;

    entry->inode = devices[*offset].inode->number;
    entry->type = INODE_DEVICE;
    copy_string(entry->name, devices[*offset].name, sizeof(entry->name));
    *offset = *offset + 1;
    return 1;
}

static void add_device(int index, const char *name, Inode *inode)
{
    inode->number = index + 1;
    inode->type = INODE_DEVICE;
    inode->mode = DEVICE_MODE;
    inode->links = 1;

    devices[index].name = name;
    devices[index].inode = inode;
}

Inode *devfs_mount()
{
    DevFolder *folder = new DevFolder();

    add_device(0, "console", new ConsoleDevice());
    add_device(1, "input", new RawInputDevice());
    add_device(2, "null", new NullDevice());
    add_device(3, "mouse", new MouseDevice());

    folder->number = 100;
    folder->type = INODE_DIRECTORY;
    folder->mode = DEVFS_MODE;
    folder->links = 2;
    return folder;
}
