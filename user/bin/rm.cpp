// rm: deletes files.

#include "libc.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(STDERR, "usage: rm <file>\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int status = unlink(argv[i]);

        if (status < 0) {
            fprintf(STDERR, "rm: %s: %s\n", argv[i], error_text(status));
            return 1;
        }
    }
    return 0;
}
