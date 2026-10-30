/* ls: lists a folder. */

#include "libc.h"

int main(int argc, char *argv[])
{
    const char *path = (argc > 1) ? argv[1] : ".";
    struct dirent entry;
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        fprintf(STDERR, "ls: %s: %s\n", path, error_text(fd));
        return 1;
    }

    while (readdir(fd, &entry) > 0) {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
            continue;
        printf("%s%s\n", entry.name, (entry.type == TYPE_DIRECTORY) ? "/" : "");
    }

    close(fd);
    return 0;
}
