// find: lists everything under a folder. usage: find [folder] [-name pattern]
// The pattern can use wildcards, in quotes so the shell leaves them alone:
//   find / -name "*.txt"

#include "libc.h"

static const char *pattern = nullptr;

static void visit(const char *path)
{
    struct dirent entry;

    if (pattern == nullptr || matches_pattern(base_name(path), pattern))
        printf("%s\n", path);

    if (!is_folder(path))
        return;

    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        fprintf(STDERR, "find: %s: %s\n", path, error_text(fd));
        return;
    }

    while (readdir(fd, &entry) > 0) {
        char inner_path[MAX_PATH];

        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
            continue;

        if (join_path(path, entry.name, inner_path))
            visit(inner_path);
    }

    close(fd);
}

int main(int argc, char *argv[])
{
    const char *start = ".";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-name") == 0 && i + 1 < argc) {
            i++;
            pattern = argv[i];
        } else if (argv[i][0] == '-') {
            fprintf(STDERR, "usage: find [folder] [-name pattern]\n");
            return 1;
        } else {
            start = argv[i];
        }
    }

    if (!is_folder(start)) {
        fprintf(STDERR, "find: %s: not a folder\n", start);
        return 1;
    }

    visit(start);
    return 0;
}
