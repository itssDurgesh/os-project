/* rmdir: deletes empty folders. */

#include "libc.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(STDERR, "usage: rmdir <folder>\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int status = rmdir(argv[i]);

        if (status < 0) {
            fprintf(STDERR, "rmdir: %s: %s\n", argv[i], error_text(status));
            return 1;
        }
    }
    return 0;
}
