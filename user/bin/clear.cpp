// clear: empties the screen.

#include "libc.h"

int main()
{
    printf("\033[2J\033[H");
    return 0;
}
