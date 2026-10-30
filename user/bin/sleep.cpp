// sleep: waits for a number of seconds.

#include "libc.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(STDERR, "usage: sleep <seconds>\n");
        return 1;
    }

    sleep(atoi(argv[1]) * 1000);
    return 0;
}
