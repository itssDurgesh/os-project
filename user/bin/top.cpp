// top: shows memory use and every process, again every 2 seconds. Ctrl+C stops it.

#include "libc.h"

#define KB_PER_PAGE 4
#define REFRESH_MS 2000

static const char *state_name(int state)
{
    static const char *const names[] = {
        "new", "ready", "running", "waiting", "zombie",
    };

    return (state >= 0 && state < 5) ? names[state] : "?";
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

        printf("PID PPID UID PRI STATE CPU NAME\n");
        for (int index = 0; procinfo(index, &info) > 0; index++) {
            printf("%d %d %d %d %s %u %s\n", info.pid, info.parent_pid, info.uid,
                   info.priority, state_name(info.state), info.cpu_ticks, info.name);
        }

        sleep(REFRESH_MS);
    }
}
