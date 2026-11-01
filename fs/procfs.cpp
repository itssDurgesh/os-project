// procfs: the /proc folder. These files aren't stored anywhere. When a program
// reads one, the kernel writes the text at that moment, so it is always current.
//   /proc/meminfo    memory and swap use
//   /proc/uptime     seconds since boot
//   /proc/processes  every process
//   /proc/date       the date and time, from the hardware clock
//   /proc/diskinfo   how much of the disk is used

#include "errno.h"
#include "hal.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "vfs.h"

#define PROC_FILE_MODE 0444
#define PROC_FOLDER_MODE 0555
#define PROC_FILE_COUNT 5
#define KB_PER_PAGE 4

// Builds up text in a buffer. The kernel has no sprintf, so this does the job.
class TextBuilder {
public:
    TextBuilder(char *new_buffer, uint32_t new_capacity)
    {
        buffer = new_buffer;
        capacity = new_capacity;
    }

    void add_text(const char *text)
    {
        while (*text != '\0' && length < capacity) {
            buffer[length] = *text;
            length++;
            text++;
        }
    }

    void add_number(uint32_t number)
    {
        char digits[12];
        int count = 0;

        do {
            digits[count] = '0' + number % 10;
            count++;
            number = number / 10;
        } while (number != 0);

        while (count > 0 && length < capacity) {
            count--;
            buffer[length] = digits[count];
            length++;
        }
    }

    // Numbers under 10 get a 0 in front, as in 09:05.
    void add_two_digits(uint32_t number)
    {
        if (number < 10)
            add_text("0");
        add_number(number);
    }

    uint32_t get_length()
    {
        return length;
    }

private:
    char *buffer;
    uint32_t capacity;
    uint32_t length = 0;
};

static void write_meminfo(TextBuilder &text)
{
    MemoryInfo info;

    get_memory_info(&info);

    text.add_text("MemTotal: ");
    text.add_number(info.total_frames * KB_PER_PAGE);
    text.add_text(" KB\nMemFree: ");
    text.add_number(info.free_frames * KB_PER_PAGE);
    text.add_text(" KB\nSwapTotal: ");
    text.add_number(info.swap_slots_total * KB_PER_PAGE);
    text.add_text(" KB\nSwapUsed: ");
    text.add_number(info.swap_slots_used * KB_PER_PAGE);
    text.add_text(" KB\nPageFaults: ");
    text.add_number(info.page_faults);
    text.add_text("\nEvictedPages: ");
    text.add_number(info.evicted_pages);
    text.add_text("\n");
}

static void write_uptime(TextBuilder &text)
{
    text.add_number(get_ticks() / TIMER_HZ);
    text.add_text(" seconds\n");
}

static void write_date(TextBuilder &text)
{
    RealTime time;

    rtc_read(&time);

    text.add_number(time.year);
    text.add_text("-");
    text.add_two_digits(time.month);
    text.add_text("-");
    text.add_two_digits(time.day);
    text.add_text(" ");
    text.add_two_digits(time.hour);
    text.add_text(":");
    text.add_two_digits(time.minute);
    text.add_text(":");
    text.add_two_digits(time.second);
    text.add_text(" UTC\n");
}

static void write_diskinfo(TextBuilder &text)
{
    uint32_t total_kb, free_kb;

    ext2_get_usage(&total_kb, &free_kb);

    text.add_text("Filesystem Total-KB Used-KB Free-KB Mounted-on\next2 ");
    text.add_number(total_kb);
    text.add_text(" ");
    text.add_number(total_kb - free_kb);
    text.add_text(" ");
    text.add_number(free_kb);
    text.add_text(" /\n");
}

static const char *state_name(int state)
{
    static const char *const names[] = { "new", "ready", "running", "waiting", "zombie" };

    return names[state];
}

static void write_processes(TextBuilder &text)
{
    for (Process *process = process_list(); process != nullptr;
         process = process->list_next) {
        text.add_number(process->pid);
        text.add_text(" ");
        text.add_number(process->uid);
        text.add_text(" ");
        text.add_text(state_name(process->state));
        text.add_text(" ");
        text.add_text(process->name);
        text.add_text("\n");
    }
}

class ProcFile : public Inode {
public:
    void (*make_text)(TextBuilder &text) = nullptr;

    int read(void *buffer, uint32_t offset, uint32_t count) override;
};

int ProcFile::read(void *buffer, uint32_t offset, uint32_t count)
{
    char *page = (char *)kmalloc(PAGE_SIZE);

    if (page == nullptr)
        return -ENOMEM;

    TextBuilder text(page, PAGE_SIZE);

    make_text(text);

    uint32_t length = text.get_length();
    int result = 0;

    if (offset < length) {
        if (count > length - offset)
            count = length - offset;

        memcpy(buffer, page + offset, count);
        result = (int)count;
    }

    kfree(page);
    return result;
}

struct ProcEntry {
    const char *name;
    ProcFile *file;
};

static ProcEntry entries[PROC_FILE_COUNT];

class ProcFolder : public Inode {
public:
    int lookup(const char *name, Inode **result) override;
    int readdir(uint32_t *offset, struct dirent *entry) override;
};

int ProcFolder::lookup(const char *name, Inode **result)
{
    Inode *found = nullptr;

    if (strcmp(name, ".") == 0)
        found = this;
    else if (strcmp(name, "..") == 0)
        found = vfs_root();

    for (int i = 0; i < PROC_FILE_COUNT; i++) {
        if (strcmp(name, entries[i].name) == 0)
            found = entries[i].file;
    }

    if (found == nullptr)
        return -ENOENT;

    hold_inode(found);
    *result = found;
    return 0;
}

int ProcFolder::readdir(uint32_t *offset, struct dirent *entry)
{
    if (*offset >= PROC_FILE_COUNT)
        return 0;

    entry->inode = entries[*offset].file->number;
    entry->type = INODE_FILE;
    copy_string(entry->name, entries[*offset].name, sizeof(entry->name));
    *offset = *offset + 1;
    return 1;
}

static void add_entry(int index, const char *name, void (*make_text)(TextBuilder &text))
{
    ProcFile *file = new ProcFile();

    file->number = index + 1;
    file->mode = PROC_FILE_MODE;
    file->links = 1;
    file->make_text = make_text;

    entries[index].name = name;
    entries[index].file = file;
}

Inode *procfs_mount()
{
    ProcFolder *folder = new ProcFolder();

    add_entry(0, "meminfo", write_meminfo);
    add_entry(1, "uptime", write_uptime);
    add_entry(2, "processes", write_processes);
    add_entry(3, "date", write_date);
    add_entry(4, "diskinfo", write_diskinfo);

    folder->number = 200;
    folder->type = INODE_DIRECTORY;
    folder->mode = PROC_FOLDER_MODE;
    folder->links = 2;
    return folder;
}
