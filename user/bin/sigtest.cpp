// Test: signals reach handlers, stop programs, and wake sleeping processes.

#include "libc.h"

#define CHECK(condition)                                            \
    do {                                                            \
        if (!(condition)) {                                         \
            printf("TEST-FAIL: %s, line %d\n", #condition, __LINE__); \
            exit(1);                                                \
        }                                                           \
    } while (0)

static volatile int times_called = 0;
static volatile int last_signal = 0;

static void my_handler(int signal_number)
{
    times_called = times_called + 1;
    last_signal = signal_number;
}

int main()
{
    int status;
    int pid;
    int pipe_ends[2];
    char letter;

    // A handler runs, then the program carries on with its registers intact.
    int checked_value = 12345;

    CHECK(set_signal_handler(SIGUSR1, my_handler) == 0);
    CHECK(kill(getpid(), SIGUSR1) == 0);
    CHECK(times_called == 1);
    CHECK(last_signal == SIGUSR1);
    CHECK(checked_value == 12345);

    // SIGTERM stops a program, and its parent can see why.
    pid = fork();
    if (pid == 0) {
        sleep(30000);
        exit(0);
    }
    sleep(50);
    CHECK(kill(pid, SIGTERM) == 0);
    wait(&status);
    CHECK(status == 128 + SIGTERM);

    // SIGKILL works even on a program that ignores everything it can.
    pid = fork();
    if (pid == 0) {
        ignore_signal(SIGTERM);
        for (;;)
            sleep(1000);
    }
    sleep(50);
    CHECK(kill(pid, SIGTERM) == 0);
    sleep(50);
    CHECK(kill(pid, SIGKILL) == 0);
    wait(&status);
    CHECK(status == 128 + SIGKILL);
    CHECK(set_signal_handler(SIGKILL, my_handler) == -EINVAL);

    // A process stuck reading an empty pipe is woken up, and sees -EINTR.
    CHECK(pipe(pipe_ends) == 0);
    pid = fork();
    if (pid == 0) {
        set_signal_handler(SIGUSR1, my_handler);
        exit((read(pipe_ends[0], &letter, 1) == -EINTR) ? 7 : 8);
    }
    sleep(50);
    CHECK(kill(pid, SIGUSR1) == 0);
    wait(&status);
    CHECK(status == 7);

    // A normal user can't signal someone else's process, and bad ids fail.
    int parent_pid = getpid();

    pid = fork();
    if (pid == 0) {
        setuid(1000);
        exit((kill(parent_pid, SIGTERM) == -EPERM) ? 0 : 1);
    }
    wait(&status);
    CHECK(status == 0);
    CHECK(kill(99999, SIGTERM) == -ESRCH);
    CHECK(kill(1, SIGKILL) == -EPERM);

    printf("TEST-PASS sigtest\n");
    return 0;
}
