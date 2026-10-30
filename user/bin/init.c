/* PID 1: sets up the console, starts the shell, and collects orphaned processes.
 * If the shell ever exits, init starts a new one. */

#include "libc.h"

int main(void)
{
    char *argv[] = { "/bin/sh", NULL };

    /* These become FDs 0, 1 and 2, which every other program inherits. */
    open("/dev/console", O_RDWR);
    open("/dev/console", O_RDWR);
    open("/dev/console", O_RDWR);

    for (;;) {
        int pid = fork();

        if (pid == 0) {
            exec(argv[0], argv);
            printf("init: can't start the shell\n");
            exit(1);
        }

        /* Runs for orphans too, until the shell itself ends. */
        while (wait(NULL) != pid)
            ;
    }
}
