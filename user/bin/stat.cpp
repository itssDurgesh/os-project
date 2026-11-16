// stat: shows the details the filesystem keeps about a file (its inode).
// usage: stat <file>...

#include "libc.h"

static const char *type_name(uint32_t type)
{
    if (type == TYPE_DIRECTORY)
        return "folder";
    if (type == TYPE_DEVICE)
        return "device";
    return "regular file";
}

// Prints a mode such as 0644 as rw-r--r--.
static void show_permissions(uint32_t mode)
{
    const char letters[] = "rwx";

    for (int bit = 8; bit >= 0; bit--)
        printf("%c", (mode & (1u << bit)) ? letters[(8 - bit) % 3] : '-');
}

static int show_file(const char *path)
{
    struct stat info;
    struct user owner;
    int status = stat(path, &info);

    if (status < 0) {
        fprintf(STDERR, "stat: %s: %s\n", path, error_text(status));
        return 1;
    }

    printf("  File:  %s\n", path);
    printf("  Type:  %s\n", type_name(info.type));
    printf("  Size:  %u bytes\n", info.size);
    printf("  Inode: %u    Links: %u\n", info.inode, info.links);
    printf("  Mode:  ");
    show_permissions(info.mode);
    printf(" (%u%u%u)\n", (info.mode >> 6) & 7, (info.mode >> 3) & 7, info.mode & 7);
    printf("  Owner: %s (uid %u)    Group: %u\n",
           user_by_uid(info.uid, &owner) ? owner.name : "?", info.uid, info.gid);
    return 0;
}

int main(int argc, char *argv[])
{
    int failed = 0;

    if (argc < 2) {
        fprintf(STDERR, "usage: stat <file>...\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (i > 1)
            printf("\n");
        failed |= show_file(argv[i]);
    }
    return failed;
}
