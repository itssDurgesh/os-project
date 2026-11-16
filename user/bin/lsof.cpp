// lsof: lists the open files of every process, or of one. usage: lsof [pid]
// A descriptor (FD) is a number that points at an open file. After fork() the
// parent and child share the same open file, so SHARED can be more than 1.

#include "libc.h"

static const char *kind_name(uint32_t kind)
{
    switch (kind) {
    case FILE_KIND_FILE:      return "file";
    case FILE_KIND_DIRECTORY: return "folder";
    case FILE_KIND_DEVICE:    return "device";
    case FILE_KIND_PIPE:      return "pipe";
    }
    return "?";
}

static const char *mode_name(uint32_t flags)
{
    uint32_t access = flags & O_ACCESS_MASK;

    if (access == O_RDWR)
        return "rw";
    if (access == O_WRONLY)
        return "w";
    return "r";
}

// Prints the files of one process. Returns how many it found.
static int show_files(int pid, const char *process_name)
{
    struct fileinfo file;
    int count = 0;
    int fd = 0;

    while ((fd = procfile(pid, fd, &file)) >= 0) {
        printf("%5d  %-10s %3d  %-6s %-3s %7u %7u %6u  %s\n", pid, process_name,
               file.fd, kind_name(file.kind), mode_name(file.flags), file.offset,
               file.size, file.shared_by, file.name);
        count++;
        fd++;
    }
    return count;
}

int main(int argc, char *argv[])
{
    struct procinfo process;
    int total = 0;

    printf("  PID  NAME        FD  TYPE   MODE OFFSET    SIZE SHARED  FILE\n");

    if (argc > 1) {
        struct procmem info;
        int pid = atoi(argv[1]);
        int status = procmem(pid, &info);

        if (status < 0) {
            fprintf(STDERR, "lsof: process %s: %s\n", argv[1], error_text(status));
            return 1;
        }
        total = show_files(pid, info.name);
    } else {
        for (int index = 0; procinfo(index, &process) > 0; index++)
            total += show_files(process.pid, process.name);
    }

    printf("\n%d open files.\n", total);
    return 0;
}
