// ps: lists every process, with the memory it was given and the RAM it uses now.
// PRI is the priority (0 is highest), CPU is timer ticks used (10 ms each).
// MEM KB is all the memory reserved for it; RAM KB is what is loaded right now.

#include "libc.h"

#define KB_PER_PAGE 4

static void memory_in_kb(int pid, uint32_t *reserved, uint32_t *ram)
{
    struct procmem info;

    *reserved = 0;
    *ram = 0;
    if (procmem(pid, &info) < 0)
        return;

    for (uint32_t i = 0; i < info.region_count; i++) {
        *reserved += (info.regions[i].end - info.regions[i].start) / 1024;
        *ram += info.regions[i].pages_in_ram * KB_PER_PAGE;
    }
}

int main()
{
    struct procinfo info;
    struct user user;

    printf("  PID  PPID  USER      STATE     PRI    CPU   MEM KB  RAM KB  NAME\n");
    for (int index = 0; procinfo(index, &info) > 0; index++) {
        const char *owner = user_by_uid(info.uid, &user) ? user.name : "?";
        uint32_t reserved, ram;

        memory_in_kb(info.pid, &reserved, &ram);
        printf("%5d %5d  %-9s %-8s %4d %6u %8u %7u  %s\n", info.pid, info.parent_pid,
               owner, process_state_name(info.state), info.priority, info.cpu_ticks,
               reserved, ram, info.name);
    }
    return 0;
}
