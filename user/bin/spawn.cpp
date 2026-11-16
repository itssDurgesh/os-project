// spawn: creates a new process that runs a program in the background, then shows
// its PID and the memory the kernel gave it. usage: spawn <program> [arguments]
// Check it with ps, and delete it with kill <pid>.

#include "libc.h"

#define KB_PER_PAGE 4
#define WAIT_STEP_MS 20
#define WAIT_STEPS 50           // wait up to 1 second for the program to start

// Waits until the child has replaced itself with the new program.
static bool wait_for_program(int pid, struct procmem *info)
{
    for (int step = 0; step < WAIT_STEPS; step++) {
        if (procmem(pid, info) < 0)
            return false;       // it already finished
        if (strcmp(info->name, "spawn") != 0)
            return true;
        sleep(WAIT_STEP_MS);
    }
    return true;
}

int main(int argc, char *argv[])
{
    char path[MAX_PATH];
    struct stat file;
    struct procmem info;
    uint32_t size[REGION_SHARED + 1];
    uint32_t total = 0;
    uint32_t ram = 0;
    int pid;

    if (argc < 2) {
        fprintf(STDERR, "usage: spawn <program> [arguments]   example: spawn sleep 60\n");
        return 1;
    }

    if (strchr(argv[1], '/') != nullptr) {
        strcpy(path, argv[1]);
    } else {
        strcpy(path, "/bin/");
        strcpy(path + 5, argv[1]);
    }

    if (stat(path, &file) < 0 || file.type != TYPE_FILE) {
        fprintf(STDERR, "spawn: %s: no such program\n", argv[1]);
        return 1;
    }

    pid = fork();
    if (pid == 0) {
        // In the background: no keyboard, and Ctrl+C doesn't stop it. kill does.
        int nothing = open("/dev/null", O_RDONLY);

        dup2(nothing, STDIN);
        close(nothing);
        ignore_signal(SIGINT);

        exec(path, &argv[1]);
        fprintf(STDERR, "spawn: can't start %s\n", path);
        exit(127);
    }

    if (pid < 0) {
        fprintf(STDERR, "spawn: can't create a process: %s\n", error_text(pid));
        return 1;
    }

    if (!wait_for_program(pid, &info)) {
        printf("Process %d (%s) started and has already finished.\n", pid, argv[1]);
        return 0;
    }

    memset(size, 0, sizeof(size));
    for (uint32_t i = 0; i < info.region_count; i++) {
        const struct memregion *region = &info.regions[i];
        uint32_t kb = (region->end - region->start) / 1024;

        size[region->kind] += kb;
        total += kb;
        ram += region->pages_in_ram * KB_PER_PAGE;
    }

    printf("Process created, running in the background:\n\n");
    printf("  PID  NAME          CODE KB  DATA KB  HEAP KB  STACK KB  TOTAL KB  RAM KB\n");
    printf("%5d  %-12s %8u %8u %8u %9u %9u %7u\n\n", info.pid, info.name,
           size[REGION_CODE], size[REGION_DATA], size[REGION_HEAP],
           size[REGION_STACK], total, ram);
    printf("ps shows it, kill %d deletes it.\n", info.pid);
    return 0;
}
