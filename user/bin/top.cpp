// top: shows memory use and every process, again every 2 seconds. Ctrl+C stops it.

#include "libc.h"

#define KB_PER_PAGE 4
#define REFRESH_MS 2000

static uint32_t ram_in_kb(int pid)
{
    struct procmem info;
    uint32_t ram = 0;

    if (procmem(pid, &info) < 0)
        return 0;

    for (uint32_t i = 0; i < info.region_count; i++)
        ram += info.regions[i].pages_in_ram * KB_PER_PAGE;
    return ram;
}

int main()
{
    for (;;) {
        MemoryInfo memory;
        struct procinfo info;

        meminfo(&memory);

        printf("\033[2J\033[H");
        printf("RAM: %u KB free of %u KB   Swap: %u KB used   Ctrl+C stops\n\n",
               memory.free_frames * KB_PER_PAGE, memory.total_frames * KB_PER_PAGE,
               memory.swap_slots_used * KB_PER_PAGE);

        printf("  PID  PPID  UID  STATE     PRI    CPU  RAM KB  NAME\n");
        for (int index = 0; procinfo(index, &info) > 0; index++) {
            printf("%5d %5d %4d  %-8s %4d %6u %7u  %s\n", info.pid, info.parent_pid,
                   info.uid, process_state_name(info.state), info.priority,
                   info.cpu_ticks, ram_in_kb(info.pid), info.name);
        }

        sleep(REFRESH_MS);
    }
}
