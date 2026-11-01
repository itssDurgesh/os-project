// cp: copies a file, or a folder and everything in it with -r. If <to> is a folder,
// the copy goes inside it.

#include "libc.h"

#define BUFFER_SIZE 4096

static char buffer[BUFFER_SIZE];

static bool copy_file(const char *from, const char *to)
{
    struct stat info;
    int input, output, length;
    int status = stat(from, &info);

    if (status < 0) {
        fprintf(STDERR, "cp: %s: %s\n", from, error_text(status));
        return false;
    }

    if (same_file(from, to)) {
        fprintf(STDERR, "cp: %s: same file as the target\n", from);
        return false;
    }

    input = open(from, O_RDONLY);
    if (input < 0) {
        fprintf(STDERR, "cp: %s: %s\n", from, error_text(input));
        return false;
    }

    output = open(to, O_WRONLY | O_CREAT | O_TRUNC, info.mode);
    if (output < 0) {
        fprintf(STDERR, "cp: %s: %s\n", to, error_text(output));
        close(input);
        return false;
    }

    while ((length = read(input, buffer, BUFFER_SIZE)) > 0)
        write(output, buffer, length);

    close(input);
    close(output);
    return length == 0;
}

static bool copy_folder(const char *from, const char *to)
{
    struct dirent entry;
    bool all_worked = true;
    int status = mkdir(to, 0755);
    int fd = open(from, O_RDONLY);

    if (status < 0 && status != -EEXIST) {
        fprintf(STDERR, "cp: %s: %s\n", to, error_text(status));
        if (fd >= 0)
            close(fd);
        return false;
    }

    if (fd < 0) {
        fprintf(STDERR, "cp: %s: %s\n", from, error_text(fd));
        return false;
    }

    while (readdir(fd, &entry) > 0) {
        char inner_from[MAX_PATH];
        char inner_to[MAX_PATH];
        bool worked;

        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
            continue;

        if (!join_path(from, entry.name, inner_from) || !join_path(to, entry.name, inner_to)) {
            fprintf(STDERR, "cp: %s: name too long\n", entry.name);
            all_worked = false;
            continue;
        }

        worked = is_folder(inner_from) ? copy_folder(inner_from, inner_to)
                                       : copy_file(inner_from, inner_to);
        if (!worked)
            all_worked = false;
    }

    close(fd);
    return all_worked;
}

int main(int argc, char *argv[])
{
    bool recursive = argc == 4 && strcmp(argv[1], "-r") == 0;
    char target[MAX_PATH];

    if (argc != 3 && !recursive) {
        fprintf(STDERR, "usage: cp [-r] <from> <to>\n");
        return 1;
    }

    const char *from = argv[recursive ? 2 : 1];
    const char *to = argv[recursive ? 3 : 2];

    if (is_folder(to)) {
        if (!join_path(to, base_name(from), target)) {
            fprintf(STDERR, "cp: %s: name too long\n", to);
            return 1;
        }
        to = target;
    }

    if (is_folder(from)) {
        if (!recursive) {
            fprintf(STDERR, "cp: %s: is a folder, so use cp -r\n", from);
            return 1;
        }
        return copy_folder(from, to) ? 0 : 1;
    }

    return copy_file(from, to) ? 0 : 1;
}
