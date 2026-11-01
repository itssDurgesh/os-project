// tail: prints the last 10 lines of files or standard input. `tail -5 file` and
// `tail -n 5 file` print 5 lines.

#include "libc.h"

#define KEEP_SIZE (1024 * 1024)  // the end of a file is all we need to keep

static char text[KEEP_SIZE];

static bool print_last_lines(int fd, int lines_wanted, const char *name)
{
    uint32_t used = 0;
    int length;

    while ((length = read(fd, text + used, KEEP_SIZE - used)) > 0) {
        used += length;

        // When the buffer fills up, throw away the older half.
        if (used == KEEP_SIZE) {
            memcpy(text, text + KEEP_SIZE / 2, KEEP_SIZE / 2);
            used = KEEP_SIZE / 2;
        }
    }

    if (length < 0) {
        fprintf(STDERR, "tail: %s: %s\n", name, error_text(length));
        return false;
    }

    // Walk back from the end, counting line endings. The very last one only ends
    // the final line, so it doesn't count.
    uint32_t start = used;
    int lines_found = 0;

    while (start > 0) {
        if (text[start - 1] == '\n' && start != used) {
            lines_found++;
            if (lines_found == lines_wanted)
                break;
        }
        start--;
    }

    if (lines_wanted > 0)
        write(STDOUT, text + start, used - start);
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
        return print_last_lines(STDIN, lines_wanted, "input") ? 0 : 1;

    for (int i = next_argument; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0) {
            fprintf(STDERR, "tail: %s: %s\n", argv[i], error_text(fd));
            all_worked = false;
            continue;
        }

        if (!print_last_lines(fd, lines_wanted, argv[i]))
            all_worked = false;
        close(fd);
    }
    return all_worked ? 0 : 1;
}
