// mv: moves or renames a file, by copying it and deleting the original.

#include "libc.h"

#define BUFFER_SIZE 4096

static char buffer[BUFFER_SIZE];

int main(int argc, char *argv[])
{
    int input, output, length;

    if (argc != 3) {
        fprintf(STDERR, "usage: mv <from> <to>\n");
        return 1;
    }

    input = open(argv[1], O_RDONLY);
    if (input < 0) {
        fprintf(STDERR, "mv: %s: %s\n", argv[1], error_text(input));
        return 1;
    }

    output = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output < 0) {
        fprintf(STDERR, "mv: %s: %s\n", argv[2], error_text(output));
        close(input);
        return 1;
    }

    while ((length = read(input, buffer, BUFFER_SIZE)) > 0)
        write(output, buffer, length);

    close(input);
    close(output);

    // Only delete the original once the copy has really worked.
    if (length < 0) {
        fprintf(STDERR, "mv: %s: %s\n", argv[1], error_text(length));
        return 1;
    }

    int status = unlink(argv[1]);

    if (status < 0) {
        fprintf(STDERR, "mv: %s: %s\n", argv[1], error_text(status));
        return 1;
    }
    return 0;
}
