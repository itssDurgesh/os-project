// free: shows how memory and swap are being used.

#include "libc.h"

#define KB_PER_PAGE 4

int main()
{
    MemoryInfo info;

    if (meminfo(&info) < 0) {
        fprintf(STDERR, "free: can't read memory information\n");
        return 1;
    }

    uint32_t ram_total = info.total_frames * KB_PER_PAGE;
    uint32_t ram_free = info.free_frames * KB_PER_PAGE;
    uint32_t swap_total = info.swap_slots_total * KB_PER_PAGE;
    uint32_t swap_used = info.swap_slots_used * KB_PER_PAGE;

    printf("          TOTAL KB    USED KB    FREE KB\n");
    printf("RAM:  %12u %10u %10u\n", ram_total, ram_total - ram_free, ram_free);
    printf("Swap: %12u %10u %10u\n", swap_total, swap_used, swap_total - swap_used);
    printf("\nPage faults: %u, pages evicted to swap: %u\n", info.page_faults,
           info.evicted_pages);
    return 0;
}
