// mem: shows the memory given to every process, then the whole system's RAM.
// Sizes are in KB. Each column is the address space reserved for that part;
// RAM and SWAP show where the pages really are right now.

#include "libc.h"

#define KB_PER_PAGE 4
#define BAR_WIDTH 30

// Draws [#####.....] filled in proportion to used / total.
static void show_bar(uint32_t used, uint32_t total)
{
    uint32_t filled = (total == 0) ? 0 : used * BAR_WIDTH / total;

    printf("[");
    for (uint32_t i = 0; i < BAR_WIDTH; i++)
        printf("%c", (i < filled) ? '#' : '.');
    printf("] %u%%", (total == 0) ? 0 : used * 100 / total);
}

int main()
{
    struct procinfo process;
    struct procmem info;
    MemoryInfo system;
    uint32_t all_ram = 0;
    int user_processes = 0;

    printf("Memory given to each process (KB)\n\n");
    printf("  PID  NAME          CODE   DATA   HEAP  STACK   MMAP SHARED     RAM   SWAP\n");

    for (int index = 0; procinfo(index, &process) > 0; index++) {
        uint32_t size[REGION_SHARED + 1];
        uint32_t ram = 0;
        uint32_t swap = 0;

        if (procmem(process.pid, &info) < 0)
            continue;   // it ended while we were looking

        memset(size, 0, sizeof(size));
        for (uint32_t i = 0; i < info.region_count; i++) {
            const struct memregion *region = &info.regions[i];

            size[region->kind] += (region->end - region->start) / 1024;
            ram += region->pages_in_ram * KB_PER_PAGE;
            swap += region->pages_in_swap * KB_PER_PAGE;
        }

        printf("%5d  %-12s", info.pid, info.name);
        if (info.region_count == 0) {
            printf("  (kernel thread: %u KB kernel stack only)\n",
                   info.kernel_stack_bytes / 1024);
            continue;
        }

        printf(" %6u %6u %6u %6u %6u %6u %7u %6u\n", size[REGION_CODE],
               size[REGION_DATA], size[REGION_HEAP], size[REGION_STACK],
               size[REGION_MMAP], size[REGION_SHARED], ram, swap);
        all_ram += ram;
        user_processes++;
    }

    if (meminfo(&system) < 0) {
        fprintf(STDERR, "mem: can't read memory information\n");
        return 1;
    }

    uint32_t total_kb = system.total_frames * KB_PER_PAGE;
    uint32_t used_kb = (system.total_frames - system.free_frames) * KB_PER_PAGE;
    uint32_t swap_total = system.swap_slots_total * KB_PER_PAGE;
    uint32_t swap_used = system.swap_slots_used * KB_PER_PAGE;

    printf("%5d  %-12s %41s %7u\n\n", user_processes, "processes", "total", all_ram);
    printf("RAM   ");
    show_bar(used_kb, total_kb);
    printf("  %u KB used of %u KB\n", used_kb, total_kb);
    printf("Swap  ");
    show_bar(swap_used, swap_total);
    printf("  %u KB used of %u KB\n", swap_used, swap_total);
    return 0;
}
