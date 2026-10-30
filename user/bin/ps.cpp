/* ps: lists every process. */

#include "libc.h"

static const char *state_name(int state)
{
    static const char *const names[] = {
        "new", "ready", "running", "waiting", "zombie",
    };

    return (state >= 0 && state < 5) ? names[state] : "?";
}

int main(void)
{
    struct procinfo info;

    printf("PID PPID UID PRI STATE CPU NAME\n");
    for (int index = 0; procinfo(index, &info) > 0; index++) {
        printf("%d %d %d %d %s %u %s\n", info.pid, info.parent_pid, info.uid,
               info.priority, state_name(info.state), info.cpu_ticks, info.name);
    }
    return 0;
}
