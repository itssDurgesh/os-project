/* cat: prints files, or standard input if no file is named. */

#include "libc.h"

#define BUFFER_SIZE 4096

static char buffer[BUFFER_SIZE];

static int print_fd(int fd)
{
    int length;

    while ((length = read(fd, buffer, BUFFER_SIZE)) > 0)
        write(STDOUT, buffer, length);
    return length;
}

int main(int argc, char *argv[])
{
    if (argc == 1)
        return (print_fd(STDIN) < 0) ? 1 : 0;

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0) {
            fprintf(STDERR, "cat: %s: %s\n", argv[i], error_text(fd));
            return 1;
        }

        int status = print_fd(fd);

        close(fd);
        if (status < 0) {
            fprintf(STDERR, "cat: %s: %s\n", argv[i], error_text(status));
            return 1;
        }
    }
    return 0;
}
