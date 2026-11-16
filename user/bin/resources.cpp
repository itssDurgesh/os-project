// resources: shows what each process is holding, and what the whole system has
// left. Pipes, message queues and shared memory are shared out by the Banker's
// Algorithm, so they are only given out when that keeps the system safe.

#include "libc.h"

#define KB_PER_PAGE 4

int main()
{
    struct procinfo process;
    struct procmem info;
    bool have_totals = false;
    struct procmem totals;

    printf("Resources held by each process\n\n");
    printf("  PID  NAME          FILES  SHM-SEGS   RAM KB  CPU-LIMIT  BANKER HELD/MAX\n");
    printf("                                                          (pipes, queues, shm)\n");

    for (int index = 0; procinfo(index, &process) > 0; index++) {
        uint32_t ram = 0;

        if (procmem(process.pid, &info) < 0)
            continue;

        for (uint32_t i = 0; i < info.region_count; i++)
            ram += info.regions[i].pages_in_ram * KB_PER_PAGE;

        printf("%5d  %-12s %6u %9u %8u  ", info.pid, info.name, info.open_files,
               info.shared_segments, ram);

        if (info.cpu_quota == 0)
            printf("%9s  ", "none");
        else
            printf("%5u/sec  ", info.cpu_quota);

        for (int type = 0; type < RESOURCE_TYPES; type++)
            printf("%d/%d ", info.resources_held[type], info.resources_maximum[type]);
        printf("\n");

        totals = info;
        have_totals = true;
    }

    if (!have_totals)
        return 1;

    printf("\nBanker's Algorithm resources for the whole system:\n");
    for (int type = 0; type < RESOURCE_TYPES; type++) {
        int total = totals.resources_total[type];
        int free_now = totals.resources_available[type];

        printf("  %-11s %d total, %d in use, %d free\n", resource_name(type), total,
               total - free_now, free_now);
    }
    return 0;
}
