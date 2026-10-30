/* pwd: prints the current folder. */

#include "libc.h"

int main(void)
{
    char path[MAX_PATH];

    getcwd(path, sizeof(path));
    printf("%s\n", path);
    return 0;
}
