// User processes: exec, fork, exit and wait.

#include <stddef.h>

#include "console.h"
#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "vfs.h"

#define USER_STACK_TOP   KERNEL_VMA
#define USER_STACK_PAGES 32
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

// Maps fresh, zeroed pages over a range. Pages already mapped are left alone,
// because two segments can share a page.
static int map_range(uint32_t *page_dir, uint32_t start, uint32_t end, uint32_t flags)
{
    for (uint32_t virt = start & PAGE_MASK; virt < end; virt += PAGE_SIZE) {
        uint32_t frame;

        if (paging_lookup(page_dir, virt) & PTE_PRESENT)
            continue;

        frame = alloc_frame();
        if (frame == 0)
            return -ENOMEM;

        memset(phys_to_virt(frame), 0, PAGE_SIZE);
        if (paging_map(page_dir, virt, frame, flags) != 0) {
            free_frame(frame);
            return -ENOMEM;
        }
    }
    return 0;
}

static int load_segment(uint32_t *page_dir, Inode *inode,
                        const struct elf_segment *segment)
{
    uint32_t flags = PTE_USER |
                     ((segment->flags & ELF_FLAG_WRITE) ? PTE_WRITABLE : 0);
    uint32_t end = segment->virt + segment->memory_size;
    uint32_t done = 0;
    uint8_t *chunk;
    int status;

    if (end < segment->virt || end > USER_STACK_TOP ||
        segment->file_size > segment->memory_size)
        return -ENOEXEC;

    status = map_range(page_dir, segment->virt, end, flags);
    if (status != 0)
        return status;

    chunk = (uint8_t *)kmalloc(PAGE_SIZE);
    if (chunk == nullptr)
        return -ENOMEM;

    while (done < segment->file_size) {
        uint32_t size = segment->file_size - done;

        if (size > PAGE_SIZE)
            size = PAGE_SIZE;

        if (inode->read(chunk, segment->offset + done, size) != (int)size) {
            kfree(chunk);
            return -ENOEXEC;
        }

        status = write_to_page_directory(page_dir, segment->virt + done, chunk, size);
        if (status != 0) {
            kfree(chunk);
            return status;
        }
        done += size;
    }

    kfree(chunk);
    return 0;
}

static int load_program(uint32_t *page_dir, Inode *inode, uint32_t *entry,
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

        status = load_segment(page_dir, inode, &segment);
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
    int status = map_range(page_dir, USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE,
                           USER_STACK_TOP, PTE_USER | PTE_WRITABLE);

    if (status != 0)
        return status;

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
    int status = vfs_lookup(current_process->cwd, path, &inode);

    if (status != 0)
        return status;

    if (inode->type != INODE_FILE) {
        release_inode(inode);
        return -EACCES;
    }

    status = vfs_check_permission(inode, current_process->uid, current_process->gid, PERMISSION_EXEC);
    if (status != 0) {
        release_inode(inode);
        return status;
    }

    page_dir = make_page_directory();
    if (page_dir == nullptr) {
        release_inode(inode);
        return -ENOMEM;
    }

    status = load_program(page_dir, inode, &entry, &heap_start);
    release_inode(inode);
    if (status == 0)
        status = build_user_stack(page_dir, argv, &user_esp);
    if (status != 0) {
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
