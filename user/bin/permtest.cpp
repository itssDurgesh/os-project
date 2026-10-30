// Acceptance test: a normal user really is limited by file permissions.

#include "libc.h"

#define STUDENT_UID 1000
#define STUDENT_GID 1000

#define CHECK(condition)                                            \
    do {                                                            \
        if (!(condition)) {                                         \
            printf("TEST-FAIL: %s, line %d\n", #condition, __LINE__); \
            exit(1);                                                \
        }                                                           \
    } while (0)

static void as_student()
{
    struct stat info;
    char buffer[16];
    int fd;

    CHECK(setgid(STUDENT_GID) == 0);
    CHECK(setuid(STUDENT_UID) == 0);
    CHECK(getuid() == STUDENT_UID);

    // Once a normal user, a process can't get root back.
    CHECK(setuid(0) == -EPERM);
    CHECK(setgid(0) == -EPERM);
    CHECK(set_echo(false) == -EPERM);

    // Files the PRD's rwx bits should protect.
    CHECK(open("/etc/shadow", O_RDONLY) == -EACCES);
    CHECK(open("/etc/hello.txt", O_WRONLY) == -EACCES);
    CHECK(open("/etc/new.txt", O_WRONLY | O_CREAT, 0644) == -EACCES);
    CHECK(unlink("/etc/hello.txt") == -EACCES);
    CHECK(rmdir("/home/student") == -EACCES);
    CHECK(mkdir("/etc/folder", 0755) == -EACCES);
    CHECK(chmod("/etc/hello.txt", 0777) == -EPERM);

    // A folder with no search permission hides what's inside it.
    CHECK(open("/tmp/secret/note.txt", O_RDONLY) == -EACCES);
    CHECK(stat("/tmp/secret/note.txt", &info) == -EACCES);

    // Ordinary files stay readable, and their own home stays writable.
    fd = open("/etc/hello.txt", O_RDONLY);
    CHECK(fd >= 0);
    CHECK(read(fd, buffer, sizeof(buffer)) == sizeof(buffer));
    close(fd);

    fd = open("/home/student/mine.txt", O_WRONLY | O_CREAT, 0600);
    CHECK(fd >= 0);
    CHECK(write(fd, "mine", 4) == 4);
    close(fd);
    CHECK(stat("/home/student/mine.txt", &info) == 0);
    CHECK(info.uid == STUDENT_UID);
    CHECK(chmod("/home/student/mine.txt", 0640) == 0);
    CHECK(unlink("/home/student/mine.txt") == 0);

    exit(0);
}

int main()
{
    int status;
    int fd;

    CHECK(getuid() == 0);

    // Root sets up a private folder.
    CHECK(mkdir("/tmp/secret", 0700) == 0);
    fd = open("/tmp/secret/note.txt", O_WRONLY | O_CREAT, 0644);
    CHECK(fd >= 0);
    close(fd);

    if (fork() == 0)
        as_student();

    wait(&status);
    CHECK(status == 0);

    CHECK(unlink("/tmp/secret/note.txt") == 0);
    CHECK(rmdir("/tmp/secret") == 0);

    printf("TEST-PASS permtest\n");
    return 0;
}
