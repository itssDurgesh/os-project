// touch: creates an empty file if it doesn't exist yet.

#include "libc.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(STDERR, "usage: touch <file>\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0644);

        if (fd < 0) {
            fprintf(STDERR, "touch: %s: %s\n", argv[i], error_text(fd));
            return 1;
        }
        close(fd);
    }
    return 0;
}
