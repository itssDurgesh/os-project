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

    printf("RAM:  %u KB used, %u KB free\n",
           (info.total_frames - info.free_frames) * KB_PER_PAGE,
           info.free_frames * KB_PER_PAGE);
    printf("Swap: %u KB used, %u KB total\n",
           info.swap_slots_used * KB_PER_PAGE, info.swap_slots_total * KB_PER_PAGE);
    printf("Page faults: %u, pages evicted: %u\n", info.page_faults, info.evicted_pages);
    return 0;
}
