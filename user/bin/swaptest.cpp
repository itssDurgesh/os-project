// Acceptance test: uses more memory than the OS keeps in RAM, so pages have to go
// out to swap and come back with every value still correct.

#include "libc.h"

#define MB (1024 * 1024)
#define PAGE 4096
#define BIG_SIZE (110 * MB)
#define FORK_SIZE (60 * MB)

// Idle programs like the shell get swapped out too, so swap is never quite empty.
// Their pages are only a few KB, so more than this would mean a slot leak.
#define SMALL_SWAP_SLOTS 64

#define CHECK(condition)                                            \
    do {                                                            \
        if (!(condition)) {                                         \
            printf("TEST-FAIL: %s, line %d\n", #condition, __LINE__); \
            exit(1);                                                \
        }                                                           \
    } while (0)

// Each page gets its own number, so a page coming back wrong would be noticed.
static void fill(char *memory, uint32_t size, int seed)
{
    for (uint32_t offset = 0; offset < size; offset += PAGE)
        *(uint32_t *)(memory + offset) = offset + seed;
}

static bool check(char *memory, uint32_t size, int seed)
{
    for (uint32_t offset = 0; offset < size; offset += PAGE) {
        if (*(uint32_t *)(memory + offset) != offset + seed)
            return false;
    }
    return true;
}

int main()
{
    MemoryInfo info;
    int status;

    // Part 1: more than fits in RAM.
    char *memory = (char *)sbrk(BIG_SIZE);

    CHECK((int)memory > 0);
    fill(memory, BIG_SIZE, 1);
    CHECK(check(memory, BIG_SIZE, 1));

    meminfo(&info);
    printf("after 110 MB: swap used %u KB, %u pages evicted, %u page faults\n",
           info.swap_slots_used * 4, info.evicted_pages, info.page_faults);
    CHECK(info.evicted_pages > 1000);
    CHECK(info.swap_slots_used > 0);

    // Part 2: giving memory back also frees its swap slots.
    sbrk(-BIG_SIZE);
    meminfo(&info);
    printf("after freeing it: swap used %u KB\n", info.swap_slots_used * 4);
    CHECK(info.swap_slots_used < SMALL_SWAP_SLOTS);

    // Part 3: fork while some of the parent's pages are in swap.
    memory = (char *)sbrk(FORK_SIZE);
    fill(memory, FORK_SIZE, 2);

    if (fork() == 0) {
        CHECK(check(memory, FORK_SIZE, 2));     // the child sees the same values
        fill(memory, FORK_SIZE, 3);             // and its own copy can change
        CHECK(check(memory, FORK_SIZE, 3));
        exit(0);
    }

    wait(&status);
    CHECK(status == 0);
    CHECK(check(memory, FORK_SIZE, 2));         // the parent's copy did not change
    sbrk(-FORK_SIZE);

    // Part 4: mmap gives zeroed memory, and munmap takes it away.
    char *mapped = (char *)mmap(8 * PAGE);

    CHECK((int)mapped > 0);
    CHECK(mapped[PAGE] == 0);
    mapped[PAGE] = 'x';
    CHECK(munmap(mapped) == 0);

    meminfo(&info);
    printf("at the end: swap used %u KB\n", info.swap_slots_used * 4);
    CHECK(info.swap_slots_used < SMALL_SWAP_SLOTS);

    printf("TEST-PASS swaptest\n");
    return 0;
}
