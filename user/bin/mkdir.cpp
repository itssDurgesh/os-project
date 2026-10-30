// mkdir: creates folders.

#include "libc.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(STDERR, "usage: mkdir <folder>\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int status = mkdir(argv[i], 0755);

        if (status < 0) {
            fprintf(STDERR, "mkdir: %s: %s\n", argv[i], error_text(status));
            return 1;
        }
    }
    return 0;
}
