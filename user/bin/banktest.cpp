// Test: Banker's Algorithm refuses a request that would leave the system unsafe,
// then grants it once it is safe. There are 8 pipes in total.

#include "libc.h"

#define CHECK(condition)                                            \
    do {                                                            \
        if (!(condition)) {                                         \
            printf("TEST-FAIL: %s, line %d\n", #condition, __LINE__); \
            exit(1);                                                \
        }                                                           \
    } while (0)

int main()
{
    int maximum[RESOURCE_TYPES] = { 6, 0, 0 };
    int too_many[RESOURCE_TYPES] = { 9, 0, 0 };
    int segment = shmget(400, 4096);
    volatile int *child_got_it = (volatile int *)shmat(segment);
    int status;

    CHECK(res_request(RESOURCE_PIPE, 1) == -EINVAL);    // must declare first
    CHECK(res_declare(too_many) == -EINVAL);            // more than exists
    CHECK(res_declare(maximum) == 0);

    // We hold 4 of 8. We might still ask for 2 more.
    CHECK(res_request(RESOURCE_PIPE, 4) == 0);
    CHECK(res_request(RESOURCE_PIPE, 3) == -EINVAL);    // more than we declared

    int child = fork();

    CHECK(child >= 0);
    if (child == 0) {
        CHECK(res_declare(maximum) == 0);

        // 3 are free, but taking them leaves 1 free while each of us could still
        // need 2 or more. Nobody could be sure to finish, so this has to wait.
        CHECK(res_request(RESOURCE_PIPE, 3) == 0);
        *child_got_it = 1;
        CHECK(res_release(RESOURCE_PIPE, 3) == 0);
        exit(0);
    }

    sleep(300);
    CHECK(*child_got_it == 0);
    printf("unsafe request is waiting, as it should\n");

    CHECK(res_release(RESOURCE_PIPE, 4) == 0);
    wait(&status);
    CHECK(status == 0);
    CHECK(*child_got_it == 1);
    printf("request granted once it was safe\n");

    printf("TEST-PASS banktest\n");
    return 0;
}
