// PID 1: sets up the console, starts login, and collects orphaned processes.
// When a user logs out, init starts login again.

#include "libc.h"

#define RETRY_DELAY_MS 1000

int main()
{
    char *argv[] = { (char *)"/bin/login", nullptr };

    ignore_signal(SIGINT);
    ignore_signal(SIGTERM);

    // These become FDs 0, 1 and 2, which every other program inherits.
    open("/dev/console", O_RDWR);
    open("/dev/console", O_RDWR);
    open("/dev/console", O_RDWR);

    for (;;) {
        int pid = fork();

        // Too many processes right now. Collect any that have ended, and try again.
        if (pid < 0) {
            waitpid(0, nullptr, true);
            sleep(RETRY_DELAY_MS);
            continue;
        }

        if (pid == 0) {
            default_signal(SIGTERM);    // only init itself should ignore this
            exec(argv[0], argv);
            printf("init: can't start login\n");
            exit(1);
        }

        // Runs for orphans too, until the login session ends.
        while (wait(nullptr) != pid)
            ;
    }
}
