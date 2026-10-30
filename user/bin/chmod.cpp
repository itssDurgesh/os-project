// chmod: changes a file's permissions. The mode is octal, such as 644.

#include "libc.h"

int main(int argc, char *argv[])
{
    int mode = 0;
    int status;

    if (argc != 3) {
        fprintf(STDERR, "usage: chmod <mode> <file>\n");
        return 1;
    }

    for (const char *digit = argv[1]; *digit != '\0'; digit++) {
        if (*digit < '0' || *digit > '7') {
            fprintf(STDERR, "chmod: bad mode %s\n", argv[1]);
            return 1;
        }
        mode = mode * 8 + (*digit - '0');
    }

    status = chmod(argv[2], mode);
    if (status < 0) {
        fprintf(STDERR, "chmod: %s: %s\n", argv[2], error_text(status));
        return 1;
    }
    return 0;
}
