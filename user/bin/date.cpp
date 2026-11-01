// date: prints the date and time. The kernel reads the hardware clock when a program
// reads /proc/date, so this only has to show that file.

#include "libc.h"

int main()
{
    char *argv[] = { (char *)"/bin/cat", (char *)"/proc/date", nullptr };

    exec(argv[0], argv);
    fprintf(STDERR, "date: can't start cat\n");
    return 1;
}
