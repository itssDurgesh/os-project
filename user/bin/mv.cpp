// mv: moves or renames a file, by copying it and deleting the original. If <to> is
// a folder, the file goes inside it. Folders can't be moved, because there is no
// rename system call.

#include "libc.h"

#define BUFFER_SIZE 4096

static char buffer[BUFFER_SIZE];

int main(int argc, char *argv[])
{
    struct stat info;
    char target[MAX_PATH];
    const char *to;
    int input, output, length;

    if (argc != 3) {
        fprintf(STDERR, "usage: mv <from> <to>\n");
        return 1;
    }

    int status = stat(argv[1], &info);

    if (status < 0) {
        fprintf(STDERR, "mv: %s: %s\n", argv[1], error_text(status));
        return 1;
    }

    // Check this first, so a failed move doesn't leave a new file behind.
    if (info.type == TYPE_DIRECTORY) {
        fprintf(STDERR, "mv: %s: folders can't be moved\n", argv[1]);
        return 1;
    }

    to = argv[2];
    if (is_folder(to)) {
        if (!join_path(to, base_name(argv[1]), target)) {
            fprintf(STDERR, "mv: %s: name too long\n", to);
            return 1;
        }
        to = target;
    }

    if (same_file(argv[1], to)) {
        fprintf(STDERR, "mv: %s: same file as the target\n", argv[1]);
        return 1;
    }

    input = open(argv[1], O_RDONLY);
    if (input < 0) {
        fprintf(STDERR, "mv: %s: %s\n", argv[1], error_text(input));
        return 1;
    }

    output = open(to, O_WRONLY | O_CREAT | O_TRUNC, info.mode);
    if (output < 0) {
        fprintf(STDERR, "mv: %s: %s\n", to, error_text(output));
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

    status = unlink(argv[1]);
    if (status < 0) {
        fprintf(STDERR, "mv: %s: %s\n", argv[1], error_text(status));
        return 1;
    }
    return 0;
}
