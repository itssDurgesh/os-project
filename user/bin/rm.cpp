// rm: deletes files. -r also deletes folders and everything in them, and -f stays
// quiet if a file doesn't exist.

#include "libc.h"

static bool recursive = false;
static bool force = false;

static int remove_path(const char *path);

// Finds one entry at a time, opening the folder again after each delete, because
// deleting while reading can make a folder skip entries.
static int remove_folder_contents(const char *path)
{
    for (;;) {
        struct dirent entry;
        char child[MAX_PATH];
        bool found = false;
        int fd = open(path, O_RDONLY);

        if (fd < 0)
            return fd;

        while (!found && readdir(fd, &entry) > 0)
            found = strcmp(entry.name, ".") != 0 && strcmp(entry.name, "..") != 0;
        close(fd);

        if (!found)
            return 0;

        if (!join_path(path, entry.name, child))
            return -EINVAL;

        int status = remove_path(child);

        if (status < 0)
            return status;
    }
}

static int remove_path(const char *path)
{
    if (recursive && is_folder(path)) {
        int status = remove_folder_contents(path);

        return (status < 0) ? status : rmdir(path);
    }
    return unlink(path);
}

int main(int argc, char *argv[])
{
    int first_file = 1;

    while (first_file < argc && argv[first_file][0] == '-') {
        for (const char *letter = argv[first_file] + 1; *letter != '\0'; letter++) {
            if (*letter == 'r') {
                recursive = true;
            } else if (*letter == 'f') {
                force = true;
            } else {
                fprintf(STDERR, "rm: unknown option -%c\n", *letter);
                return 1;
            }
        }
        first_file++;
    }

    if (first_file == argc) {
        fprintf(STDERR, "usage: rm [-r] [-f] <file>\n");
        return 1;
    }

    for (int i = first_file; i < argc; i++) {
        int status = remove_path(argv[i]);

        if (status == -ENOENT && force)
            continue;

        if (status < 0) {
            fprintf(STDERR, "rm: %s: %s\n", argv[i], error_text(status));
            return 1;
        }
    }
    return 0;
}
