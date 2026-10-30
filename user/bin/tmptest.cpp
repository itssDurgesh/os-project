// Test: tmpfs on /tmp, and the /proc files.

#include "libc.h"

#define BIG_SIZE 100000

#define CHECK(condition)                                            \
    do {                                                            \
        if (!(condition)) {                                         \
            printf("TEST-FAIL: %s, line %d\n", #condition, __LINE__); \
            exit(1);                                                \
        }                                                           \
    } while (0)

static char buffer[BIG_SIZE];
static char text[1024];

static void test_tmpfs()
{
    struct stat info;
    struct dirent entry;
    int fd;
    int found = 0;

    CHECK(mkdir("/tmp/work", 0755) == 0);
    CHECK(mkdir("/tmp/work", 0755) == -EEXIST);

    // A file big enough that it has to grow many times.
    memset(buffer, 'a', BIG_SIZE);
    fd = open("/tmp/work/big.txt", O_WRONLY | O_CREAT, 0644);
    CHECK(fd >= 0);
    CHECK(write(fd, buffer, BIG_SIZE) == BIG_SIZE);
    close(fd);

    CHECK(stat("/tmp/work/big.txt", &info) == 0);
    CHECK(info.size == BIG_SIZE);

    memset(buffer, 0, BIG_SIZE);
    fd = open("/tmp/work/big.txt", O_RDONLY);
    CHECK(read(fd, buffer, BIG_SIZE) == BIG_SIZE);
    close(fd);
    for (int i = 0; i < BIG_SIZE; i++)
        CHECK(buffer[i] == 'a');

    // Listing, and the rules for deleting.
    fd = open("/tmp/work", O_RDONLY);
    while (readdir(fd, &entry) > 0)
        found++;
    close(fd);
    CHECK(found == 1);
    CHECK(rmdir("/tmp/work") == -ENOTEMPTY);

    // A deleted file stays readable until it is closed.
    fd = open("/tmp/work/big.txt", O_RDONLY);
    CHECK(unlink("/tmp/work/big.txt") == 0);
    CHECK(stat("/tmp/work/big.txt", &info) == -ENOENT);
    CHECK(read(fd, buffer, 10) == 10);
    close(fd);

    // Paths through the mount point work both ways.
    CHECK(chdir("/tmp/work") == 0);
    CHECK(stat("../../etc/hello.txt", &info) == 0);
    CHECK(chdir("/") == 0);
    CHECK(rmdir("/tmp/work") == 0);

    // A /tmp file truly isn't on the disk, so a pipe through it still works.
    fd = open("/tmp/note.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(write(fd, "hello", 5) == 5);
    close(fd);
    CHECK(unlink("/tmp/note.txt") == 0);
}

static int read_whole_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    int length;

    CHECK(fd >= 0);
    length = read(fd, text, sizeof(text) - 1);
    close(fd);
    CHECK(length > 0);
    text[length] = '\0';
    return length;
}

static bool text_has(const char *wanted)
{
    size_t wanted_length = strlen(wanted);

    for (size_t i = 0; text[i] != '\0'; i++) {
        if (strncmp(text + i, wanted, wanted_length) == 0)
            return true;
    }
    return false;
}

static void test_proc()
{
    struct stat info;

    read_whole_file("/proc/meminfo");
    CHECK(text_has("MemTotal:"));
    CHECK(text_has("SwapTotal: 32768 KB"));

    read_whole_file("/proc/uptime");
    CHECK(text_has("seconds"));

    read_whole_file("/proc/processes");
    CHECK(text_has("tmptest"));         // it can see itself running
    CHECK(text_has("swapper"));         // and the kernel's own threads
    CHECK(text_has("sh"));

    CHECK(stat("/proc/missing", &info) == -ENOENT);

    // Root may open anything, but /proc files still can't be written.
    int fd = open("/proc/meminfo", O_WRONLY);

    CHECK(fd >= 0);
    CHECK(write(fd, "x", 1) == -EINVAL);
    close(fd);
}

int main()
{
    test_tmpfs();
    test_proc();

    printf("TEST-PASS tmptest\n");
    return 0;
}
