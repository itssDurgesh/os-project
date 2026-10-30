/* The small C library for user programs (owner: Integration). */

#ifndef LIBC_H
#define LIBC_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "abi.h"
#include "errno.h"

#define STDIN  0
#define STDOUT 1
#define STDERR 2

/* System calls. On failure they return a negative error number. */
int fork(void);
int exec(const char *path, char *const argv[]);
extern "C" void exit(int status) __attribute__((noreturn));
int wait(int *status);
int getpid(void);
int getuid(void);
int getgid(void);
int sleep(uint32_t milliseconds);
void *sbrk(int increment);
int open(const char *path, int flags, ...);
int close(int fd);
int read(int fd, void *buffer, uint32_t count);
int write(int fd, const void *buffer, uint32_t count);
int seek(int fd, int offset, int whence);
int stat(const char *path, struct stat *result);
int unlink(const char *path);
int mkdir(const char *path, int mode);
int rmdir(const char *path);
int readdir(int fd, struct dirent *entry);
int chdir(const char *path);
int getcwd(char *buffer, uint32_t size);
int dup2(int old_fd, int new_fd);
int pipe(int fds[2]);
int setuid(int uid);
int setgid(int gid);
int set_echo(bool enabled);
int procinfo(int index, struct procinfo *info);
int chmod(const char *path, int mode);

/* Strings and memory */
size_t strlen(const char *text);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t count);
char *strcpy(char *dest, const char *src);
char *strchr(const char *text, int c);
extern "C" void *memset(void *dest, int value, size_t count);
extern "C" void *memcpy(void *dest, const void *src, size_t count);
extern "C" int memcmp(const void *a, const void *b, size_t count);
int atoi(const char *text);

/* Output. Supports %d %u %x %s %c %%. */
int printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int fprintf(int fd, const char *format, ...) __attribute__((format(printf, 2, 3)));
const char *error_text(int error);

/* Users: read from /etc/passwd and /etc/shadow */
struct user {
    char name[32];
    int uid;
    int gid;
    char home[64];
    char shell[64];
};

bool user_by_name(const char *name, struct user *result);
bool user_by_uid(int uid, struct user *result);
bool user_check_password(const char *name, const char *password);

/* SHA-256. `digest` receives 32 bytes. */
void sha256(const void *data, size_t length, uint8_t digest[32]);

/* Heap */
void *malloc(size_t size);
void free(void *pointer);

#endif
