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
#define KEY_ESCAPE 27

#define DEVICE_MODE 0666
#define DEVFS_MODE  0755

#define LINE_LENGTH 256
#define HISTORY_SIZE 16
#define DEVICE_COUNT 4

static char line[LINE_LENGTH];
static uint32_t line_length;
static uint32_t line_position;
static uint32_t line_cursor;     // where in the line the next letter goes
static bool echo_enabled = true;

static char history[HISTORY_SIZE][LINE_LENGTH];
static int history_count = 0;   // how many lines were ever saved
static int history_position;    // which old line is showing now

void console_set_echo(bool enabled)
{
    echo_enabled = enabled;
}

// The screen cursor moves with escape codes, because that also works on a real
// terminal. Nothing is shown while a password is being typed.
static void move_left(uint32_t amount)
{
    if (echo_enabled && amount > 0)
        kprintf("\033[%uD", amount);
}

static void move_right(uint32_t amount)
{
    if (echo_enabled && amount > 0)
        kprintf("\033[%uC", amount);
}

// Prints the line from the cursor to the end, clears anything left over from
// before, and puts the screen cursor back where it was.
static void redraw_from_cursor()
{
    if (!echo_enabled)
        return;

    for (uint32_t i = line_cursor; i < line_length; i++)
        kprintf("%c", line[i]);

    kprintf("\033[K");
    move_left(line_length - line_cursor);
}

static void insert_letter(char c)
{
    if (line_length >= LINE_LENGTH - 1)
        return;

    for (uint32_t i = line_length; i > line_cursor; i--)
        line[i] = line[i - 1];

    line[line_cursor] = c;
    line_length++;
    redraw_from_cursor();
    line_cursor++;
    move_right(1);
}

// Removes the letter under the cursor, and closes the gap.
static void remove_letter()
{
    for (uint32_t i = line_cursor; i + 1 < line_length; i++)
        line[i] = line[i + 1];

    line_length--;
    redraw_from_cursor();
}

static void backspace_key()
{
    if (line_cursor > 0) {
        line_cursor--;
        move_left(1);
        remove_letter();
    }
}

static void delete_key()
{
    if (line_cursor < line_length)
        remove_letter();
}

static void move_cursor_to(uint32_t new_position)
{
    if (new_position < line_cursor)
        move_left(line_cursor - new_position);
    else
        move_right(new_position - line_cursor);

    line_cursor = new_position;
}

// Shows another line in place of what has been typed.
static void replace_line(const char *new_text)
{
    move_cursor_to(0);
    line_length = 0;

    for (const char *c = new_text; *c != '\0' && *c != '\n'; c++) {
        line[line_length] = *c;
        line_length++;
    }

    redraw_from_cursor();
    move_cursor_to(line_length);
}

// The up arrow goes back through old lines, and the down arrow comes forward.
static void show_history_line(char arrow)
{
    int oldest = (history_count > HISTORY_SIZE) ? history_count - HISTORY_SIZE : 0;

    if (arrow == 'A' && history_position > oldest) {
        history_position--;
        replace_line(history[history_position % HISTORY_SIZE]);
    } else if (arrow == 'B' && history_position < history_count) {
        history_position++;
        if (history_position == history_count)
            replace_line("");
        else
            replace_line(history[history_position % HISTORY_SIZE]);
    }
}

static void save_to_history()
{
    // Passwords are typed with echo off, and they must never be saved.
    if (!echo_enabled || line_length <= 1)
        return;

    copy_string(history[history_count % HISTORY_SIZE], line, LINE_LENGTH);
    history[history_count % HISTORY_SIZE][line_length - 1] = '\0';
    history_count++;
}

// Special keys arrive as ESC [ and a letter. Delete, and some terminals' Home and
// End, have a number and a ~ instead, like ESC [ 3 ~.
static void handle_special_key()
{
    char bracket, code;

    if (!input_get_char(&bracket) || !input_get_char(&code))
        return;

    if (bracket != '[' && bracket != 'O')
        return;

    if (code >= '0' && code <= '9') {
        char tilde;

        if (!input_get_char(&tilde))
            return;

        if (code == '3')
            delete_key();
        else if (code == '1' || code == '7')
            move_cursor_to(0);
        else if (code == '4' || code == '8')
            move_cursor_to(line_length);
    } else if (code == 'A' || code == 'B') {
        if (echo_enabled)
            show_history_line(code);
    } else if (code == 'C') {
        if (line_cursor < line_length)
            move_cursor_to(line_cursor + 1);
    } else if (code == 'D') {
        if (line_cursor > 0)
            move_cursor_to(line_cursor - 1);
    } else if (code == 'H') {
        move_cursor_to(0);
    } else if (code == 'F') {
        move_cursor_to(line_length);
    }
}

// Reads a whole line, and lets the person edit it with the arrow keys, Backspace,
// Delete, Home and End. Returns 1 for a line, 0 for Ctrl+D, or -EINTR if a signal
// arrived.
static int read_line()
{
    line_length = 0;
    line_position = 0;
    line_cursor = 0;
    history_position = history_count;

    for (;;) {
        char c;

        if (!input_get_char(&c)) {
            line_length = 0;
            return -EINTR;
        }

        if (c == KEY_END_OF_FILE && line_length == 0)
            return 0;

        if (c == '\r')
            c = '\n';

        if (c == KEY_ESCAPE) {
            handle_special_key();
        } else if (c == KEY_BACKSPACE || c == KEY_DELETE) {
            backspace_key();
        } else if (c == '\n') {
            // Enter sends the whole line, wherever the cursor is.
            line[line_length] = '\n';
            line_length++;
            if (echo_enabled)
                kprintf("\n");
            save_to_history();
            return 1;
        } else if (c >= ' ') {
            insert_letter(c);
        }
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
