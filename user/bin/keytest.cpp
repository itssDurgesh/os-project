// keytest: shows the number the OS receives for each key. It's for checking a
// keyboard: an arrow key should show 27, 91, and then a letter's number.
// Press q to stop.

#include "libc.h"

int main()
{
    int input = open("/dev/input", O_RDONLY);
    char key;

    if (input < 0) {
        fprintf(STDERR, "keytest: can't read the keyboard\n");
        return 1;
    }

    printf("Press keys to see their numbers. Press q to stop.\n");

    while (read(input, &key, 1) == 1 && key != 'q')
        printf("%d\n", (int)(unsigned char)key);

    close(input);
    return 0;
}
