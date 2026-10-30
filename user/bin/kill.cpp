// kill: sends a signal to a process. usage: kill <pid> [signal number]

#include "libc.h"

int main(int argc, char *argv[])
{
    int signal_number = SIGTERM;
    int status;

    if (argc < 2) {
        fprintf(STDERR, "usage: kill <pid> [signal number]\n");
        return 1;
    }

    if (argc > 2)
        signal_number = atoi(argv[2]);

    status = kill(atoi(argv[1]), signal_number);
    if (status < 0) {
        fprintf(STDERR, "kill: %s\n", error_text(status));
        return 1;
    }
    return 0;
}
