// pwd: prints the current folder.

#include "libc.h"

int main()
{
    char path[MAX_PATH];

    getcwd(path, sizeof(path));
    printf("%s\n", path);
    return 0;
}
