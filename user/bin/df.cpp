// df: shows how much of the disk is used. The kernel works this out when a program
// reads /proc/diskinfo, so this only has to show that file.

#include "libc.h"

int main()
{
    char *argv[] = { (char *)"/bin/cat", (char *)"/proc/diskinfo", nullptr };

    exec(argv[0], argv);
    fprintf(STDERR, "df: can't start cat\n");
    return 1;
}
