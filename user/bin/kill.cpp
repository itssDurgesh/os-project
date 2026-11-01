// kill: sends a signal to a process. usage: kill [-signal] <pid>
// The older form, kill <pid> <signal>, also works.

#include "libc.h"

int main(int argc, char *argv[])
{
    int signal_number = SIGTERM;
    int pid_argument = 1;
    int status;

    if (argc > 1 && argv[1][0] == '-') {
        signal_number = atoi(argv[1] + 1);
        pid_argument = 2;
    }

    if (argc <= pid_argument) {
        fprintf(STDERR, "usage: kill [-signal] <pid>\n");
        return 1;
    }

    if (argc > pid_argument + 1)
        signal_number = atoi(argv[pid_argument + 1]);

    status = kill(atoi(argv[pid_argument]), signal_number);
    if (status < 0) {
        fprintf(STDERR, "kill: %s\n", error_text(status));
        return 1;
    }
    return 0;
}
