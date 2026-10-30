/* PID 1: sets up the console, starts login, and collects orphaned processes.
 * When a user logs out, init starts login again. */

#include "libc.h"

int main(void)
{
    char *argv[] = { "/bin/login", NULL };

    /* These become FDs 0, 1 and 2, which every other program inherits. */
    open("/dev/console", O_RDWR);
    open("/dev/console", O_RDWR);
    open("/dev/console", O_RDWR);

    for (;;) {
        int pid = fork();

        if (pid == 0) {
            exec(argv[0], argv);
            printf("init: can't start login\n");
            exit(1);
        }

        /* Runs for orphans too, until the login session ends. */
        while (wait(NULL) != pid)
            ;
    }
}
