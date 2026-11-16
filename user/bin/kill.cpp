// kill: sends a signal to a process. usage: kill [-signal] <pid>
// The older form, kill <pid> <signal>, also works. With no signal it sends
// SIGTERM (15), which deletes the process unless it catches it. -9 (SIGKILL)
// can't be caught, so it always deletes the process.

#include "libc.h"

#define KB_PER_PAGE 4
#define WAIT_STEP_MS 20
#define WAIT_STEPS 25           // wait up to half a second for it to end

static uint32_t ram_in_kb(const struct procmem *info)
{
    uint32_t ram = 0;

    for (uint32_t i = 0; i < info->region_count; i++)
        ram += info->regions[i].pages_in_ram * KB_PER_PAGE;
    return ram;
}

// True once the process is gone, or has ended and only waits to be collected.
static bool has_ended(int pid)
{
    struct procmem info;

    for (int step = 0; step < WAIT_STEPS; step++) {
        if (procmem(pid, &info) < 0 || info.state == 4)   // 4 = zombie
            return true;
        sleep(WAIT_STEP_MS);
    }
    return false;
}

int main(int argc, char *argv[])
{
    int signal_number = SIGTERM;
    int pid_argument = 1;
    struct procmem info;
    int status;
    int pid;

    if (argc > 1 && argv[1][0] == '-') {
        signal_number = atoi(argv[1] + 1);
        pid_argument = 2;
    }

    if (argc <= pid_argument) {
        fprintf(STDERR, "usage: kill [-signal] <pid>\n");
        return 1;
    }

    if (argc > pid_argument + 1)
        signal_number = atoi(argv[pid_argument + 1]);

    pid = atoi(argv[pid_argument]);
    status = procmem(pid, &info);
    if (status < 0) {
        fprintf(STDERR, "kill: process %d: %s\n", pid, error_text(status));
        return 1;
    }

    status = kill(pid, signal_number);
    if (status < 0) {
        fprintf(STDERR, "kill: process %d: %s\n", pid, error_text(status));
        return 1;
    }

    if (has_ended(pid)) {
        printf("  PID  NAME          SIGNAL  RAM FREED KB\n");
        printf("%5d  %-12s %7d %13u\n", pid, info.name, signal_number, ram_in_kb(&info));
    } else {
        printf("kill: sent signal %d to process %d (%s); it is still running\n",
               signal_number, pid, info.name);
    }
    return 0;
}
