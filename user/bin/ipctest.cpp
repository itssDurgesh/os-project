// Test: shared memory, semaphores and message queues between real processes.

#include "libc.h"

#define CHECK(condition)                                            \
    do {                                                            \
        if (!(condition)) {                                         \
            printf("TEST-FAIL: %s, line %d\n", #condition, __LINE__); \
            exit(1);                                                \
        }                                                           \
    } while (0)

#define WORKERS 4
#define ROUNDS 3000

static void test_shared_memory_and_semaphores()
{
    int segment = shmget(100, 4096);
    int lock = sem_open(200, 1);
    int status;

    CHECK(segment >= 0);
    CHECK(lock >= 0);

    volatile int *counter = (volatile int *)shmat(segment);

    CHECK((int)counter > 0);
    CHECK(*counter == 0);       // new shared memory starts as zeros

    // This is the PRD's locktest. Without the semaphore, updates would get lost.
    for (int worker = 0; worker < WORKERS; worker++) {
        if (fork() == 0) {
            for (int round = 0; round < ROUNDS; round++) {
                sem_wait(lock);
                int value = *counter;

                if (round % 100 == 0)
                    sleep(1);   // hold the lock while someone else tries to run
                *counter = value + 1;
                sem_post(lock);
            }
            exit(0);
        }
    }

    for (int worker = 0; worker < WORKERS; worker++) {
        wait(&status);
        CHECK(status == 0);
    }

    printf("shared counter: %d\n", *counter);
    CHECK(*counter == WORKERS * ROUNDS);

    // Attaching again by key reaches the same memory.
    CHECK(shmget(100, 4096) == segment);
    CHECK(shmdt((void *)counter) == 0);
    CHECK(sem_close(lock) == 0);
}

static void test_message_queues()
{
    int queue = msgget(300);
    char text[32];
    int status;

    CHECK(queue >= 0);

    int child = fork();

    if (child == 0) {
        msgsnd(queue, 1, "first", 6);
        msgsnd(queue, 2, "second", 7);
        msgsnd(queue, 1, "third", 6);
        exit(0);
    }

    // Asking for type 2 skips the older message of type 1.
    CHECK(msgrcv(queue, 2, text, sizeof(text)) == 7);
    CHECK(strcmp(text, "second") == 0);

    // Type 0 means any type, oldest first.
    CHECK(msgrcv(queue, 0, text, sizeof(text)) == 6);
    CHECK(strcmp(text, "first") == 0);
    CHECK(msgrcv(queue, 1, text, sizeof(text)) == 6);
    CHECK(strcmp(text, "third") == 0);

    wait(&status);
    CHECK(status == 0);

    // Too big, or a bad queue, fails cleanly.
    CHECK(msgsnd(queue, 1, text, MAX_MESSAGE_SIZE + 1) == -EINVAL);
    CHECK(msgsnd(77, 1, text, 1) == -EINVAL);
}

int main()
{
    test_shared_memory_and_semaphores();
    test_message_queues();

    printf("TEST-PASS ipctest\n");
    return 0;
}
