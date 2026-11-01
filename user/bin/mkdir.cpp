// mkdir: creates folders. -p also creates any missing folders on the way, and stays
// quiet if the folder already exists.

#include "libc.h"

// Makes each folder along the path, such as a, then a/b, then a/b/c.
static int make_all_folders(const char *path)
{
    char part[MAX_PATH];
    int length = strlen(path);

    if (length >= MAX_PATH)
        return -ENAMETOOLONG;

    for (int i = 1; i <= length; i++) {
        if (path[i] == '/' || path[i] == '\0') {
            memcpy(part, path, i);
            part[i] = '\0';

            // Skip folders that exist. Trying to make /home would be refused for
            // a normal user, even though only /home/student/new is needed.
            if (is_folder(part))
                continue;

            int status = mkdir(part, 0755);

            if (status < 0)
                return status;
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    bool make_parents = argc > 1 && strcmp(argv[1], "-p") == 0;
    int first_folder = make_parents ? 2 : 1;

    if (argc <= first_folder) {
        fprintf(STDERR, "usage: mkdir [-p] <folder>\n");
        return 1;
    }

    for (int i = first_folder; i < argc; i++) {
        int status = make_parents ? make_all_folders(argv[i]) : mkdir(argv[i], 0755);

        if (status < 0) {
            fprintf(STDERR, "mkdir: %s: %s\n", argv[i], error_text(status));
            return 1;
        }
    }
    return 0;
}
