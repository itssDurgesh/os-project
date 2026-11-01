// uname: prints the name of this OS. -a also prints its version and CPU type.

#include "libc.h"

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "-a") == 0)
        printf("OS Project 1.0 i386\n");
    else
        printf("OS Project\n");
    return 0;
}
