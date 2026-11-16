// pmap: shows each part of one process's memory. usage: pmap <pid>
// With no PID it shows its own memory.

#include "libc.h"

#define KB_PER_PAGE 4

static void show_owner(int uid)
{
    struct user user;

    if (user_by_uid(uid, &user))
        printf("%s", user.name);
    else
        printf("uid %d", uid);
}

int main(int argc, char *argv[])
{
    struct procmem info;
    int pid = (argc > 1) ? atoi(argv[1]) : 0;
    int status = procmem(pid, &info);
    uint32_t total_size = 0;
    uint32_t total_ram = 0;
    uint32_t total_swap = 0;

    if (status < 0) {
        fprintf(STDERR, "pmap: process %s: %s\n", argv[1], error_text(status));
        return 1;
    }

    printf("Memory of process %d (%s), owner ", info.pid, info.name);
    show_owner(info.uid);
    printf(", state %s\n\n", process_state_name(info.state));

    if (info.region_count == 0) {
        printf("This is a kernel thread. It has no user memory, only a kernel stack.\n");
    } else {
        printf("PART    START       END            SIZE    IN RAM   IN SWAP  ACCESS\n");
        for (uint32_t i = 0; i < info.region_count; i++) {
            const struct memregion *region = &info.regions[i];
            uint32_t size_kb = (region->end - region->start) / 1024;

            printf("%-6s  0x%08x  0x%08x %7u KB %6u KB %6u KB  %s\n",
                   region_name(region->kind), region->start, region->end, size_kb,
                   region->pages_in_ram * KB_PER_PAGE,
                   region->pages_in_swap * KB_PER_PAGE,
                   region->writable ? "read-write" : "read-only");

            total_size += size_kb;
            total_ram += region->pages_in_ram * KB_PER_PAGE;
            total_swap += region->pages_in_swap * KB_PER_PAGE;
        }
        printf("------------------------------------------------------------------\n");
        printf("Total                          %7u KB %6u KB %6u KB\n\n",
               total_size, total_ram, total_swap);
    }

    printf("KERNEL STACK  PAGE TABLES  PCB\n");
    printf("%9u KB %9u KB  %u bytes\n", info.kernel_stack_bytes / 1024,
           info.page_table_pages * KB_PER_PAGE, info.pcb_bytes);
    return 0;
}
