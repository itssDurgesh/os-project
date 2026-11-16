// Process memory reports: what the kernel has given to each process.
//
// pmap, mem, ps and resources ask for this through the SYS_PROCMEM system call.
// When a process has `report_memory` turned on (the shell's `memlog on`), the
// kernel also prints one short report when it starts a program with exec(), and
// one when it ends, showing the RAM it gives back.

#include "console.h"
#include "errno.h"
#include "hal.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "syscall.h"
#include "vfs.h"

#define KB_PER_PAGE (PAGE_SIZE / 1024)
#define PAGE_TABLE_SPAN (1024 * PAGE_SIZE)  // one page table covers 4 MB
#define USER_DIRECTORY_ENTRIES (KERNEL_VMA / PAGE_TABLE_SPAN)
#define EXIT_STATUS_SIGNAL_BASE 128

// Counts the pages of [start, end) that are in RAM and in swap. Skips 4 MB at a
// time where there is no page table, so a big empty area is still quick.
static void count_pages(const uint32_t *page_dir, uint32_t start, uint32_t end,
                        uint32_t *in_ram, uint32_t *in_swap)
{
    uint32_t virt = start;

    *in_ram = 0;
    *in_swap = 0;

    while (virt < end) {
        if ((page_dir[virt / PAGE_TABLE_SPAN] & PTE_PRESENT) == 0) {
            virt = (virt / PAGE_TABLE_SPAN + 1) * PAGE_TABLE_SPAN;
            continue;
        }

        uint32_t entry = paging_lookup(page_dir, virt);

        if (entry & PTE_PRESENT)
            *in_ram = *in_ram + 1;
        else if (entry & PTE_SWAPPED)
            *in_swap = *in_swap + 1;

        virt += PAGE_SIZE;
    }
}

// Works out what a memory area is for.
static uint32_t area_kind(const Process *process, int index)
{
    const MemoryArea *area = &process->areas[index];

    if (index == process->heap_area)
        return REGION_HEAP;
    if (area->end == KERNEL_VMA)
        return REGION_STACK;
    if (area->file != nullptr)
        return (area->flags & PTE_WRITABLE) ? REGION_DATA : REGION_CODE;
    return REGION_MMAP;
}

static bool has_own_address_space(const Process *process)
{
    return process->page_dir != nullptr && process->page_dir != kernel_page_dir;
}

// The page directory, plus each page table for the user half of memory.
static uint32_t count_page_tables(const Process *process)
{
    uint32_t count = 1;

    if (!has_own_address_space(process))
        return 0;

    for (uint32_t i = 0; i < USER_DIRECTORY_ENTRIES; i++) {
        if (process->page_dir[i] & PTE_PRESENT)
            count++;
    }
    return count;
}

// Fills in `info`. Interrupts are off, so the process can't end halfway through.
void fill_process_memory(Process *process, struct procmem *info)
{
    uint32_t flags = turn_off_interrupts();
    int total[RESOURCE_TYPES];
    int available[RESOURCE_TYPES];

    memset(info, 0, sizeof(*info));
    info->pid = process->pid;
    info->parent_pid = process->parent_pid;
    info->uid = process->uid;
    info->state = process->state;
    copy_string(info->name, process->name, PROCESS_NAME_LENGTH);

    if (has_own_address_space(process)) {
        for (int i = 0; i < MAX_MEMORY_AREAS; i++) {
            const MemoryArea *area = &process->areas[i];
            struct memregion *region = &info->regions[info->region_count];

            if (!area->in_use)
                continue;

            region->kind = area_kind(process, i);
            region->start = area->start;
            region->end = area->end;
            region->writable = (area->flags & PTE_WRITABLE) != 0;
            count_pages(process->page_dir, area->start, area->end,
                        &region->pages_in_ram, &region->pages_in_swap);
            info->region_count++;
        }

        for (int slot = 0; slot < MAX_ATTACHED_SEGMENTS; slot++) {
            struct memregion *region = &info->regions[info->region_count];
            uint32_t start, pages;

            if (!shared_memory_slot(process, slot, &start, &pages))
                continue;

            region->kind = REGION_SHARED;
            region->start = start;
            region->end = start + pages * PAGE_SIZE;
            region->writable = 1;
            region->pages_in_ram = pages;   // shared pages always stay in RAM
            info->region_count++;
            info->shared_segments++;
        }
    }

    info->kernel_stack_bytes = (process->kernel_stack != nullptr) ? KERNEL_STACK_SIZE : 0;
    info->page_table_pages = count_page_tables(process);
    info->pcb_bytes = sizeof(Process);

    if (process->files != nullptr) {
        for (int fd = 0; fd < MAX_OPEN_FILES; fd++) {
            if (process->files[fd] != nullptr)
                info->open_files++;
        }
    }

    info->cpu_quota = process->cpu_quota;
    info->pending_signals = process->pending_signals;

    banker_get_totals(total, available);
    for (int type = 0; type < RESOURCE_TYPES; type++) {
        info->resources_held[type] = process->banker_allocated[type];
        info->resources_maximum[type] = process->banker_declared ?
                                        process->banker_maximum[type] : 0;
        info->resources_total[type] = total[type];
        info->resources_available[type] = available[type];
    }

    restore_interrupts(flags);
}

// How many of this process's own pages are in RAM. Shared pages are left out,
// because they don't belong to one process alone.
uint32_t process_pages_in_ram(Process *process)
{
    uint32_t flags = turn_off_interrupts();
    uint32_t pages = 0;

    if (has_own_address_space(process)) {
        for (int i = 0; i < MAX_MEMORY_AREAS; i++) {
            const MemoryArea *area = &process->areas[i];
            uint32_t in_ram, in_swap;

            if (!area->in_use)
                continue;

            count_pages(process->page_dir, area->start, area->end, &in_ram, &in_swap);
            pages += in_ram;
        }
    }

    restore_interrupts(flags);
    return pages;
}

// SYS_PROCMEM: ebx = pid (0 means me), ecx = struct procmem to fill in.
int sys_procmem(Registers *r)
{
    int pid = (int)r->ebx;
    struct procmem *output = (struct procmem *)r->ecx;
    Process *process;

    if (!user_range_valid(output, sizeof(*output), true))
        return -EFAULT;

    struct procmem *info = (struct procmem *)kmalloc(sizeof(struct procmem));

    if (info == nullptr)
        return -ENOMEM;

    process = (pid == 0) ? current_process : find_process(pid);
    if (process == nullptr) {
        kfree(info);
        return -ESRCH;
    }

    fill_process_memory(process, info);
    memcpy(output, info, sizeof(*info));
    kfree(info);
    return 0;
}

// SYS_MEMREPORT: ebx = 1 to turn reports on for this process and its children.
int sys_memreport(Registers *r)
{
    current_process->report_memory = r->ebx != 0;
    return 0;
}

// Called by exec() once the new program's memory is set up. Prints one table row.
void report_new_program(Process *process, const char *path)
{
    struct procmem *info = (struct procmem *)kmalloc(sizeof(struct procmem));
    uint32_t size[REGION_SHARED + 1];
    uint32_t total = 0;
    uint32_t ram = 0;

    (void)path;
    if (info == nullptr)
        return;

    fill_process_memory(process, info);
    memset(size, 0, sizeof(size));

    for (uint32_t i = 0; i < info->region_count; i++) {
        const struct memregion *region = &info->regions[i];
        uint32_t kb = (region->end - region->start) / 1024;

        size[region->kind] += kb;
        total += kb;
        ram += region->pages_in_ram * KB_PER_PAGE;
    }

    kprintf("[created]  PID  CODE KB  DATA KB  HEAP KB  STACK KB  TOTAL KB  RAM KB  NAME\n");
    kprintf("          %4d %8u %8u %8u %9u %9u %7u  %s\n", process->pid,
            size[REGION_CODE], size[REGION_DATA], size[REGION_HEAP],
            size[REGION_STACK], total, ram, process->name);
    kfree(info);
}

// Called at the start of exit, before anything is freed. Prints one line.
void report_process_end(Process *process, int status)
{
    uint32_t ram_kb = process_pages_in_ram(process) * KB_PER_PAGE;

    kprintf("[ended]    PID %d (%s), status %d, RAM freed %u KB\n", process->pid,
            process->name, status, ram_kb);
}
