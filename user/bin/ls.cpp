// ls: lists folders and files. -l also shows permissions, owner and size, and -a
// also shows the . and .. entries.

#include "libc.h"

static bool long_format = false;
static bool show_all = false;

static void print_mode(const struct stat *info)
{
    static const char letters[] = "rwxrwxrwx";

    printf("%c", (info->type == TYPE_DIRECTORY) ? 'd' :
                 (info->type == TYPE_DEVICE) ? 'c' : '-');
    for (int i = 0; i < 9; i++)
        printf("%c", (info->mode & (0400 >> i)) ? letters[i] : '-');
}

static void print_long(const char *path, const char *name)
{
    struct stat info;
    struct user user;

    if (stat(path, &info) < 0) {
        printf("?????????? %s\n", name);
        return;
    }

    print_mode(&info);
    if (user_by_uid(info.uid, &user))
        printf(" %s", user.name);
    else
        printf(" %u", info.uid);
    printf(" %u %s\n", info.size, name);
}

static bool list_folder(const char *path)
{
    struct dirent entry;
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        fprintf(STDERR, "ls: %s: %s\n", path, error_text(fd));
        return false;
    }

    while (readdir(fd, &entry) > 0) {
        bool is_dot = strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0;

        if (is_dot && !show_all)
            continue;

        if (long_format) {
            char full_path[MAX_PATH];

            if (join_path(path, entry.name, full_path))
                print_long(full_path, entry.name);
            else
                printf("?????????? %s\n", entry.name);
        } else {
            printf("%s%s\n", entry.name, (entry.type == TYPE_DIRECTORY) ? "/" : "");
        }
    }

    close(fd);
    return true;
}

static bool list_path(const char *path, bool show_heading)
{
    struct stat info;
    int status = stat(path, &info);

    if (status < 0) {
        fprintf(STDERR, "ls: %s: %s\n", path, error_text(status));
        return false;
    }

    if (info.type != TYPE_DIRECTORY) {
        if (long_format)
            print_long(path, path);
        else
            printf("%s\n", path);
        return true;
    }

    if (show_heading)
        printf("%s:\n", path);
    return list_folder(path);
}

int main(int argc, char *argv[])
{
    int path_count = 0;
    bool all_worked = true;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') {
            path_count++;
            continue;
        }

        for (const char *letter = argv[i] + 1; *letter != '\0'; letter++) {
            if (*letter == 'l') {
                long_format = true;
            } else if (*letter == 'a') {
                show_all = true;
            } else {
                fprintf(STDERR, "ls: unknown option -%c\n", *letter);
                return 1;
            }
        }
    }

    if (path_count == 0)
        return list_path(".", false) ? 0 : 1;

    for (int i = 1; i < argc; i++) {
        if ((argv[i][0] != '-' || argv[i][1] == '\0') &&
            !list_path(argv[i], path_count > 1))
            all_worked = false;
    }
    return all_worked ? 0 : 1;
}
