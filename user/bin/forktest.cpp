/* Acceptance test: processes, exit status, zombies, memory isolation, a crash
 * that doesn't take the OS down, and bad pointers. */

#include "libc.h"

#define CHILDREN 20

#define CHECK(condition)                                            \
    do {                                                            \
        if (!(condition)) {                                         \
            printf("TEST-FAIL: %s, line %d\n", #condition, __LINE__); \
            exit(1);                                                \
        }                                                           \
    } while (0)

static int shared_value = 1;

int main(void)
{
    int status;
    int pid;

    /* Every child runs, and every exit status comes back to the parent. */
    int total = 0;

    for (int i = 0; i < CHILDREN; i++) {
        pid = fork();
        CHECK(pid >= 0);
        if (pid == 0)
            exit(i);
    }
    for (int i = 0; i < CHILDREN; i++) {
        CHECK(wait(&status) > 0);
        total += status;
    }
    CHECK(total == CHILDREN * (CHILDREN - 1) / 2);
    CHECK(wait(&status) == -ECHILD);    /* no zombies left */

    /* A child's memory is a separate copy. */
    pid = fork();
    if (pid == 0) {
        shared_value = 99;
        exit(shared_value);
    }
    wait(&status);
    CHECK(status == 99);
    CHECK(shared_value == 1);

    /* A crashing program only ends itself. */
    volatile int * volatile target = (volatile int *)0x10;

    pid = fork();
    if (pid == 0) {
        *target = 1;
        exit(0);
    }
    wait(&status);
    CHECK(status == 139);

    /* Programs can't touch kernel memory, even by asking the kernel to. */
    target = (volatile int *)0xC0100000;
    pid = fork();
    if (pid == 0) {
        *target = 1;
        exit(0);
    }
    wait(&status);
    CHECK(status == 139);
    CHECK(write(STDOUT, (void *)0xC0100000, 16) == -EFAULT);
    CHECK(read(STDIN, (void *)0x10, 16) == -EFAULT);

    /* An unknown system call fails cleanly. */
    int result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(999));
    CHECK(result == -ENOSYS);

    /* malloc() returns separate, usable blocks. */
    char *blocks[16];

    for (int i = 0; i < 16; i++) {
        blocks[i] = (char *)malloc(5000 + i * 100);
        CHECK(blocks[i] != NULL);
        memset(blocks[i], i + 1, 5000 + i * 100);
    }
    for (int i = 0; i < 16; i++) {
        for (int byte = 0; byte < 5000 + i * 100; byte++)
            CHECK(blocks[i][byte] == i + 1);
        free(blocks[i]);
    }

    printf("TEST-PASS forktest\n");
    return 0;
}
