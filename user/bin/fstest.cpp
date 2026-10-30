/* Acceptance test: creates, writes, reads and deletes files, including one big
 * enough to need indirect blocks. */

#include "libc.h"

#define BIG_SIZE 300000         /* well past the 48 KB of direct blocks */
#define CHUNK 1000

#define CHECK(condition)                                            \
    do {                                                            \
        if (!(condition)) {                                         \
            printf("TEST-FAIL: %s, line %d\n", #condition, __LINE__); \
            exit(1);                                                \
        }                                                           \
    } while (0)

static char buffer[CHUNK];

static void fill(uint32_t position)
{
    for (int i = 0; i < CHUNK; i++)
        buffer[i] = (char)((position + i) * 31);
}

int main(void)
{
    struct stat info;
    struct dirent entry;
    int fd, other;
    int found = 0;

    CHECK(mkdir("/home/test", 0755) == 0);
    CHECK(mkdir("/home/test", 0755) == -EEXIST);

    /* A small file, read back through a second descriptor. */
    fd = open("/home/test/small.txt", O_WRONLY | O_CREAT, 0644);
    CHECK(fd >= 0);
    CHECK(write(fd, "hello", 5) == 5);
    close(fd);

    fd = open("/home/test/small.txt", O_RDONLY);
    CHECK(fd >= 0);
    CHECK(read(fd, buffer, CHUNK) == 5);
    CHECK(memcmp(buffer, "hello", 5) == 0);
    CHECK(read(fd, buffer, CHUNK) == 0);        /* end of file */
    close(fd);

    /* Appending keeps what's already there. */
    fd = open("/home/test/small.txt", O_WRONLY | O_APPEND);
    CHECK(write(fd, " world", 6) == 6);
    close(fd);
    CHECK(stat("/home/test/small.txt", &info) == 0);
    CHECK(info.size == 11);

    /* A big file, with every byte checked. */
    fd = open("/home/test/big.bin", O_WRONLY | O_CREAT, 0644);
    CHECK(fd >= 0);
    for (uint32_t position = 0; position < BIG_SIZE; position += CHUNK) {
        fill(position);
        CHECK(write(fd, buffer, CHUNK) == CHUNK);
    }
    close(fd);

    CHECK(stat("/home/test/big.bin", &info) == 0);
    CHECK(info.size == BIG_SIZE);

    fd = open("/home/test/big.bin", O_RDONLY);
    for (uint32_t position = 0; position < BIG_SIZE; position += CHUNK) {
        char expected[CHUNK];

        fill(position);
        memcpy(expected, buffer, CHUNK);
        CHECK(read(fd, buffer, CHUNK) == CHUNK);
        CHECK(memcmp(buffer, expected, CHUNK) == 0);
    }

    /* Seeking back reads the same data again. */
    CHECK(seek(fd, 1000, SEEK_SET) == 1000);
    fill(1000);
    {
        char expected[CHUNK];

        memcpy(expected, buffer, CHUNK);
        CHECK(read(fd, buffer, CHUNK) == CHUNK);
        CHECK(memcmp(buffer, expected, CHUNK) == 0);
    }
    close(fd);

    /* After fork, parent and child share one file position. */
    fd = open("/home/test/small.txt", O_RDONLY);
    if (fork() == 0) {
        read(fd, buffer, 6);
        exit(0);
    }
    wait(NULL);
    CHECK(read(fd, buffer, 5) == 5);
    CHECK(memcmp(buffer, "world", 5) == 0);
    close(fd);

    /* Directory listing. */
    fd = open("/home/test", O_RDONLY);
    while (readdir(fd, &entry) > 0)
        found++;
    close(fd);
    CHECK(found == 4);                          /* ., .., small.txt, big.bin */

    /* Errors, and a folder that isn't empty. */
    CHECK(open("/home/test/missing", O_RDONLY) == -ENOENT);
    CHECK(rmdir("/home/test") == -ENOTEMPTY);
    CHECK(unlink("/home/test") == -EISDIR);

    /* A deleted file stays readable until the last user closes it. */
    fd = open("/home/test/small.txt", O_RDONLY);
    CHECK(unlink("/home/test/small.txt") == 0);
    CHECK(stat("/home/test/small.txt", &info) == -ENOENT);
    CHECK(read(fd, buffer, 5) == 5);
    close(fd);

    other = unlink("/home/test/big.bin");
    CHECK(other == 0);
    CHECK(rmdir("/home/test") == 0);
    CHECK(stat("/home/test", &info) == -ENOENT);

    /* Leave a file behind, so the test can check it survives a reboot. */
    fd = open("/home/persist.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(write(fd, "saved to disk\n", 14) == 14);
    close(fd);

    printf("TEST-PASS fstest\n");
    return 0;
}
