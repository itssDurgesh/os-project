// User processes: exec, fork, exit and wait.

#include <stddef.h>

#include "console.h"
#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "vfs.h"

#define USER_STACK_TOP   KERNEL_VMA
#define USER_STACK_PAGES 256  // 1 MB, loaded a page at a time
#define STACK_PAGES_AT_START 3
#define USER_EFLAGS      0x202  // interrupts on

#define ELF_MAGIC  0x464C457F  // 0x7F 'E' 'L' 'F'
#define ELF_CLASS32 1
#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_386 3
#define ELF_SEGMENT_LOAD 1
#define ELF_FLAG_WRITE 2

#define INIT_PID 1
#define EXIT_STATUS_FAULT 139

struct elf_header {
    uint32_t magic;
    uint8_t file_class;
    uint8_t rest_of_ident[11];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t program_header_offset;
    uint32_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_size;
    uint16_t program_header_count;
    uint16_t section_header_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
};

struct elf_segment {
    uint32_t type;
    uint32_t offset;
    uint32_t virt;
    uint32_t phys;
    uint32_t file_size;
    uint32_t memory_size;
    uint32_t flags;
    uint32_t align;
};

extern "C" void enter_user_mode(const Registers *r) __attribute__((noreturn));
extern "C" void return_from_interrupt();

static WaitQueue child_exited;

void process_init()
{
}

// Records one part of the program file. Nothing is read until a page is used.
static int add_program_segment(MemoryArea *areas, Inode *inode,
                               const struct elf_segment *segment)
{
    uint32_t flags = PTE_USER;
    uint32_t end = segment->virt + segment->memory_size;
    uint32_t start = segment->virt & PAGE_MASK;
    uint32_t lead_in = segment->virt - start;   // bytes before the segment's start

    if (segment->flags & ELF_FLAG_WRITE)
        flags |= PTE_WRITABLE;

    if (end < segment->virt || end > USER_STACK_TOP ||
        segment->file_size > segment->memory_size || lead_in > segment->offset)
        return -ENOEXEC;

    int area = add_memory_area(areas, start, round_up_to_page(end), flags, inode,
                               segment->offset - lead_in,
                               segment->file_size + lead_in);

    return (area < 0) ? area : 0;
}

static int read_program_header(MemoryArea *areas, Inode *inode, uint32_t *entry,
                               uint32_t *heap_start)
{
    struct elf_header header;
    uint32_t top = 0;

    if (inode->read(&header, 0, sizeof(header)) != sizeof(header))
        return -ENOEXEC;

    if (header.magic != ELF_MAGIC || header.file_class != ELF_CLASS32 ||
        header.type != ELF_TYPE_EXEC || header.machine != ELF_MACHINE_386)
        return -ENOEXEC;

    for (uint32_t i = 0; i < header.program_header_count; i++) {
        struct elf_segment segment;
        uint32_t offset = header.program_header_offset + i * sizeof(segment);
        int status;

        if (inode->read(&segment, offset, sizeof(segment)) != sizeof(segment))
            return -ENOEXEC;

        if (segment.type != ELF_SEGMENT_LOAD)
            continue;

        status = add_program_segment(areas, inode, &segment);
        if (status != 0)
            return status;

        if (segment.virt + segment.memory_size > top)
            top = segment.virt + segment.memory_size;
    }

    *entry = header.entry;
    *heap_start = round_up_to_page(top);
    return 0;
}

// Builds the stack a new program starts with: argc, then the argv pointers,
// with the strings above them.
static int build_user_stack(uint32_t *page_dir, char *const argv[], uint32_t *user_esp)
{
    uint32_t stack = USER_STACK_TOP;
    uint32_t pointers[MAX_ARGUMENTS + 1];
    uint32_t argc = 0;
    int status;

    // The strings and pointers go in now, so these pages can't wait for a fault.
    for (uint32_t page = 1; page <= STACK_PAGES_AT_START; page++) {
        status = map_new_user_page(page_dir, USER_STACK_TOP - page * PAGE_SIZE,
                                   PTE_USER | PTE_WRITABLE);
        if (status != 0)
            return status;
    }

    while (argv[argc] != nullptr) {
        uint32_t length = strlen(argv[argc]) + 1;

        if (argc == MAX_ARGUMENTS)
            return -EINVAL;

        stack -= length;
        status = write_to_page_directory(page_dir, stack, argv[argc], length);
        if (status != 0)
            return status;

        pointers[argc++] = stack;
    }
    pointers[argc] = 0;

    stack &= ~3u;
    stack -= (argc + 1) * sizeof(uint32_t);
    status = write_to_page_directory(page_dir, stack, pointers, (argc + 1) * sizeof(uint32_t));
    if (status != 0)
        return status;

    stack -= sizeof(uint32_t);
    status = write_to_page_directory(page_dir, stack, &argc, sizeof(argc));
    if (status != 0)
        return status;

    *user_esp = stack;
    return 0;
}

static void set_name_from_path(const char *path)
{
    const char *name = path;

    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/')
            name = p + 1;
    }
    copy_string(current_process->name, name, PROCESS_NAME_LENGTH);
}

// Replaces the current program. `path` and `argv` must be kernel copies, because
// the old user memory is gone by the end.
int process_exec(const char *path, char *const argv[], Registers *r)
{
    Inode *inode;
    uint32_t *page_dir;
    uint32_t *old_page_dir = current_process->page_dir;
    uint32_t entry, heap_start, user_esp;
    MemoryArea new_areas[MAX_MEMORY_AREAS];
    int heap_area = -1;
    int status = vfs_lookup(current_process->cwd, path, &inode);

    if (status != 0)
        return status;

    if (inode->type != INODE_FILE) {
        release_inode(inode);
        return -EACCES;
    }

    status = vfs_check_permission(inode, current_process->uid, current_process->gid,
                                  PERMISSION_EXEC);
    if (status != 0) {
        release_inode(inode);
        return status;
    }

    page_dir = make_page_directory();
    if (page_dir == nullptr) {
        release_inode(inode);
        return -ENOMEM;
    }

    memset(new_areas, 0, sizeof(new_areas));

    status = read_program_header(new_areas, inode, &entry, &heap_start);
    release_inode(inode);       // the areas keep their own references

    if (status == 0)
        status = add_memory_area(new_areas, USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE,
                                 USER_STACK_TOP, PTE_USER | PTE_WRITABLE, nullptr, 0, 0);
    if (status >= 0) {
        heap_area = add_memory_area(new_areas, heap_start, heap_start,
                                    PTE_USER | PTE_WRITABLE, nullptr, 0, 0);
        status = (heap_area < 0) ? heap_area : 0;
    }
    if (status == 0)
        status = build_user_stack(page_dir, argv, &user_esp);

    if (status != 0) {
        release_memory_areas(new_areas);
        delete_page_directory(page_dir);
        return status;
    }

    // The old program's memory is going away, so nothing stays attached to it.
    // Handlers go too, but signals being ignored stay ignored, as in Unix.
    shared_memory_detach_all(current_process);
    for (int i = 0; i < NUM_SIGNALS; i++) {
        if (current_process->signal_handlers[i] != SIG_IGN)
            current_process->signal_handlers[i] = SIG_DFL;
    }

    release_memory_areas(current_process->areas);
    memcpy(current_process->areas, new_areas, sizeof(new_areas));
    current_process->heap_area = heap_area;
    current_process->next_mmap_address = 0;

    current_process->page_dir = page_dir;
    current_process->heap_end = heap_start;
    paging_switch(page_dir);
    if (old_page_dir != kernel_page_dir)
        delete_page_directory(old_page_dir);

    set_name_from_path(path);

    memset(r, 0, sizeof(*r));
    r->ds = r->es = r->fs = r->gs = USER_DATA_SELECTOR;
    r->user_ss = USER_DATA_SELECTOR;
    r->cs = USER_CODE_SELECTOR;
    r->eip = entry;
    r->user_esp = user_esp;
    r->eflags = USER_EFLAGS;
    return 0;
}

// Turns the calling kernel thread into the first user process, PID 1.
void process_start_first(const char *path)
{
    char *argv[] = { (char *)path, nullptr };
    Registers *frame =
        (Registers *)(current_process->kernel_stack_top - sizeof(Registers));
    int status;

    current_process->files = (File **)kmalloc_zeroed(MAX_OPEN_FILES * sizeof(File *));
    if (current_process->files == nullptr)
        panic("no memory for the first process");

    hold_inode(vfs_root());
    current_process->cwd = vfs_root();
    copy_string(current_process->cwd_path, "/", MAX_PATH);

    status = process_exec(path, argv, frame);
    if (status != 0)
        panic("can't start %s: error %d", path, status);

    enter_user_mode(frame);
}

int process_fork(const Registers *r)
{
    Process *child = create_process(current_process->name, current_process->base_priority);
    Registers *child_regs;
    uint32_t *stack;

    if (child == nullptr)
        return -ENOMEM;

    kernel_stack_alloc(child);

    child->uid = current_process->uid;
    child->gid = current_process->gid;
    child->heap_end = current_process->heap_end;
    child->heap_area = current_process->heap_area;
    child->next_mmap_address = current_process->next_mmap_address;
    copy_memory_areas(current_process->areas, child->areas);
    child->cpu_quota = current_process->cpu_quota;
    for (int i = 0; i < NUM_SIGNALS; i++)
        child->signal_handlers[i] = current_process->signal_handlers[i];
    child->signal_return_address = current_process->signal_return_address;
    shared_memory_copy_to_child(current_process, child);
    child->page_dir = copy_page_directory(current_process->page_dir);
    child->files = (File **)kmalloc_zeroed(MAX_OPEN_FILES * sizeof(File *));

    if (child->page_dir == nullptr || child->files == nullptr) {
        if (child->page_dir != nullptr)
            delete_page_directory(child->page_dir);
        release_memory_areas(child->areas);
        kfree(child->files);
        delete_process(child);
        return -ENOMEM;
    }

    // Parent and child share each open file, including its position.
    for (int fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (current_process->files[fd] != nullptr) {
            hold_file(current_process->files[fd]);
            child->files[fd] = current_process->files[fd];
        }
    }

    hold_inode(current_process->cwd);
    child->cwd = current_process->cwd;
    copy_string(child->cwd_path, current_process->cwd_path, MAX_PATH);

    // The child wakes up returning from the same system call, with result 0.
    child_regs = (Registers *)(child->kernel_stack_top - sizeof(Registers));
    *child_regs = *r;
    child_regs->eax = 0;

    stack = (uint32_t *)child_regs;
    *--stack = (uint32_t)return_from_interrupt;
    *--stack = 0;  // ebx
    *--stack = 0;  // esi
    *--stack = 0;  // edi
    *--stack = 0;  // ebp
    child->saved_esp = (uint32_t)stack;

    add_new_process(child);
    return child->pid;
}

void process_exit(int status)
{
    uint32_t *old_page_dir = current_process->page_dir;

    enable_interrupts();

    banker_process_exit(current_process);
    shared_memory_detach_all(current_process);
    release_memory_areas(current_process->areas);

    for (int fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (current_process->files[fd] != nullptr) {
            file_close(current_process->files[fd]);
            current_process->files[fd] = nullptr;
        }
    }
    kfree(current_process->files);
    current_process->files = nullptr;

    release_inode(current_process->cwd);
    current_process->cwd = nullptr;

    paging_switch(kernel_page_dir);
    current_process->page_dir = kernel_page_dir;
    delete_page_directory(old_page_dir);

    // Orphans are adopted by init, which collects them.
    for (Process *process = process_list(); process != nullptr;
         process = process->list_next) {
        if (process->parent_pid == current_process->pid)
            process->parent_pid = INIT_PID;
    }

    Process *parent = find_process(current_process->parent_pid);

    if (parent != nullptr)
        send_signal(parent, SIGCHLD);

    child_exited.wake_all();
    thread_exit(status);
}

int process_wait(int *status)
{
    for (;;) {
        bool has_children = false;

        for (Process *process = process_list(); process != nullptr;
             process = process->list_next) {
            if (process->parent_pid != current_process->pid)
                continue;

            has_children = true;
            if (process->state == PROCESS_TERMINATED) {
                int pid = process->pid;

                *status = process->exit_status;
                delete_process(process);
                return pid;
            }
        }

        if (!has_children)
            return -ECHILD;

        child_exited.sleep(true);
        if (signal_pending())
            return -EINTR;
    }
}

// A program did something illegal. Only that program stops.
void process_fault(const Registers *r, const char *reason)
{
    kprintf("%s: %s at %p, process ended\n", current_process->name, reason, (void *)r->eip);
    process_exit(EXIT_STATUS_FAULT);
}
