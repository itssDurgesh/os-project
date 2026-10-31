// head: prints the first 10 lines of a file. `head -5 file` prints 5 lines.

#include "libc.h"

#define BUFFER_SIZE 4096

static char buffer[BUFFER_SIZE];

int main(int argc, char *argv[])
{
    int lines_wanted = 10;
    int lines_done = 0;
    int next_argument = 1;
    int fd = STDIN;
    int length;

    if (argc > 1 && argv[1][0] == '-') {
        lines_wanted = atoi(argv[1] + 1);
        next_argument = 2;
    }

    if (argc > next_argument) {
        fd = open(argv[next_argument], O_RDONLY);
        if (fd < 0) {
            fprintf(STDERR, "head: %s: %s\n", argv[next_argument], error_text(fd));
            return 1;
        }
    }

    while (lines_done < lines_wanted && (length = read(fd, buffer, BUFFER_SIZE)) > 0) {
        int end = 0;

        while (end < length && lines_done < lines_wanted) {
            if (buffer[end] == '\n')
                lines_done++;
            end++;
        }
        write(STDOUT, buffer, end);
    }
    return 0;
}
