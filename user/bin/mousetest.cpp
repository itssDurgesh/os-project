// mousetest: prints mouse movements from /dev/mouse. The test moves the mouse
// through QEMU and checks these numbers.

#include "libc.h"

#define EVENTS_TO_READ 2

int main()
{
    MouseEvent event;
    int fd = open("/dev/mouse", O_RDONLY);

    if (fd < 0) {
        fprintf(STDERR, "mousetest: can't open /dev/mouse\n");
        return 1;
    }

    printf("waiting for the mouse\n");

    for (int i = 0; i < EVENTS_TO_READ; i++) {
        if (read(fd, &event, sizeof(event)) != sizeof(event)) {
            printf("TEST-FAIL: bad mouse read\n");
            return 1;
        }

        printf("mouse moved x=%d y=%d buttons=%d\n", event.x_move, event.y_move,
               event.buttons);
    }

    close(fd);
    printf("TEST-PASS mouse\n");
    return 0;
}
