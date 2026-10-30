/* ls: lists a folder. With -l it also shows permissions, owner and size. */

#include "libc.h"

static void print_mode(const struct stat *info)
{
    static const char letters[] = "rwxrwxrwx";

    printf("%c", (info->type == TYPE_DIRECTORY) ? 'd' :
                 (info->type == TYPE_DEVICE) ? 'c' : '-');
    for (int i = 0; i < 9; i++)
        printf("%c", (info->mode & (0400 >> i)) ? letters[i] : '-');
}

static void print_long(const char *folder, const struct dirent *entry)
{
    char path[MAX_PATH];
    struct stat info;
    struct user user;
    size_t length = strlen(folder);

    strcpy(path, folder);
    if (length > 0 && path[length - 1] != '/')
        path[length++] = '/';
    strcpy(path + length, entry->name);

    if (stat(path, &info) < 0) {
        printf("?????????? %s\n", entry->name);
        return;
    }

    print_mode(&info);
    if (user_by_uid(info.uid, &user))
        printf(" %s", user.name);
    else
        printf(" %u", info.uid);
    printf(" %u %s\n", info.size, entry->name);
}

int main(int argc, char *argv[])
{
    bool long_format = argc > 1 && strcmp(argv[1], "-l") == 0;
    int path_index = long_format ? 2 : 1;
    const char *path = (argc > path_index) ? argv[path_index] : ".";
    struct dirent entry;
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        fprintf(STDERR, "ls: %s: %s\n", path, error_text(fd));
        return 1;
    }

    while (readdir(fd, &entry) > 0) {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
            continue;
        if (long_format)
            print_long(path, &entry);
        else
            printf("%s%s\n", entry.name, (entry.type == TYPE_DIRECTORY) ? "/" : "");
    }

    close(fd);
    return 0;
}
