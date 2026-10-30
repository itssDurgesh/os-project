/* wc: counts lines, words and bytes in a file or standard input. */

#include "libc.h"

#define BUFFER_SIZE 4096

static char buffer[BUFFER_SIZE];

int main(int argc, char *argv[])
{
    int fd = STDIN;
    uint32_t lines = 0, words = 0, bytes = 0;
    bool in_word = false;
    int length;

    if (argc > 1) {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
            fprintf(STDERR, "wc: %s: %s\n", argv[1], error_text(fd));
            return 1;
        }
    }

    while ((length = read(fd, buffer, BUFFER_SIZE)) > 0) {
        bytes += length;
        for (int i = 0; i < length; i++) {
            char c = buffer[i];
            bool space = c == ' ' || c == '\n' || c == '\t';

            if (c == '\n')
                lines++;
            if (!space && !in_word)
                words++;
            in_word = !space;
        }
    }

    printf("%u %u %u\n", lines, words, bytes);
    return 0;
}
