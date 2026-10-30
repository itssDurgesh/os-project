// Test: a process with a CPU quota really gets less CPU time.

#include "libc.h"

#define CHECK(condition)                                            \
    do {                                                            \
        if (!(condition)) {                                         \
            printf("TEST-FAIL: %s, line %d\n", #condition, __LINE__); \
            exit(1);                                                \
        }                                                           \
    } while (0)

#define RUN_SECONDS 2
#define QUOTA_TICKS 20          // 20 of the 100 ticks in each second

static uint32_t cpu_ticks_of(int pid)
{
    struct procinfo info;

    for (int index = 0; procinfo(index, &info) > 0; index++) {
        if (info.pid == pid)
            return info.cpu_ticks;
    }
    return 0;
}

// Runs a busy child for a while and returns how much CPU it got.
static uint32_t measure(int quota)
{
    int pid = fork();
    uint32_t ticks;
    int status;

    if (pid == 0) {
        for (;;)
            ;
    }

    if (quota != 0)
        set_quota(pid, quota);

    sleep(RUN_SECONDS * 1000);
    ticks = cpu_ticks_of(pid);
    kill(pid, SIGKILL);
    wait(&status);
    return ticks;
}

int main()
{
    uint32_t without_quota = measure(0);
    uint32_t with_quota = measure(QUOTA_TICKS);

    printf("cpu ticks without quota: %u, with quota: %u\n", without_quota, with_quota);

    CHECK(with_quota <= (RUN_SECONDS + 1) * QUOTA_TICKS + 5);
    CHECK(without_quota > with_quota * 2);

    printf("TEST-PASS quotatest\n");
    return 0;
}
