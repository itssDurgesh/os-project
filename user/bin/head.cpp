// head: prints the first 10 lines of files or standard input. `head -5 file` and
// `head -n 5 file` print 5 lines.

#include "libc.h"

#define BUFFER_SIZE 4096

static char buffer[BUFFER_SIZE];

static bool print_first_lines(int fd, int lines_wanted, const char *name)
{
    int lines_done = 0;
    int length = 0;

    while (lines_done < lines_wanted && (length = read(fd, buffer, BUFFER_SIZE)) > 0) {
        int end = 0;

        while (end < length && lines_done < lines_wanted) {
            if (buffer[end] == '\n')
                lines_done++;
            end++;
        }
        write(STDOUT, buffer, end);
    }

    if (length < 0) {
        fprintf(STDERR, "head: %s: %s\n", name, error_text(length));
        return false;
    }
    return true;
}

int main(int argc, char *argv[])
{
    int lines_wanted = 10;
    int next_argument = 1;
    bool all_worked = true;

    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        lines_wanted = atoi(argv[2]);
        next_argument = 3;
    } else if (argc > 1 && argv[1][0] == '-') {
        // Both -5 and -n5 mean 5 lines.
        lines_wanted = atoi((argv[1][1] == 'n') ? argv[1] + 2 : argv[1] + 1);
        next_argument = 2;
    }

    if (next_argument == argc)
        return print_first_lines(STDIN, lines_wanted, "input") ? 0 : 1;

    for (int i = next_argument; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0) {
            fprintf(STDERR, "head: %s: %s\n", argv[i], error_text(fd));
            all_worked = false;
            continue;
        }

        if (!print_first_lines(fd, lines_wanted, argv[i]))
            all_worked = false;
        close(fd);
    }
    return all_worked ? 0 : 1;
}
