// The small C library for user programs (owner: Integration).

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

// System calls. On failure they return a negative error number.
int fork();
int exec(const char *path, char *const argv[]);
extern "C" void exit(int status) __attribute__((noreturn));
int wait(int *status);
int waitpid(int pid, int *status, bool do_not_sleep);
int getpid();
int getuid();
int getgid();
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
int kill(int pid, int signal_number);
int set_signal_handler(int signal_number, void (*handler)(int));
int ignore_signal(int signal_number);
int default_signal(int signal_number);
int set_quota(int pid, int ticks_per_second);
void *mmap(uint32_t size);
int munmap(void *address);
int meminfo(MemoryInfo *info);

// Message queues
int msgget(int key);
int msgsnd(int id, int type, const void *data, uint32_t size);
int msgrcv(int id, int type, void *data, uint32_t max_size);

// Shared memory
int shmget(int key, uint32_t size);
void *shmat(int id);
int shmdt(void *address);

// Semaphores
int sem_open(int key, int starting_value);
int sem_wait(int id);
int sem_post(int id);
int sem_close(int id);

// Banker's Algorithm
int res_declare(const int maximum[RESOURCE_TYPES]);
int res_request(int type, int amount);
int res_release(int type, int amount);

// Strings and memory
size_t strlen(const char *text);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t count);
char *strcpy(char *dest, const char *src);
char *strchr(const char *text, int c);
extern "C" void *memset(void *dest, int value, size_t count);
extern "C" void *memcpy(void *dest, const void *src, size_t count);
extern "C" int memcmp(const void *a, const void *b, size_t count);
int atoi(const char *text);

// Output. Supports %d %u %x %s %c %%.
int printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int fprintf(int fd, const char *format, ...) __attribute__((format(printf, 2, 3)));
const char *error_text(int error);

// Users: read from /etc/passwd and /etc/shadow
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

// SHA-256. `digest` receives 32 bytes.
void sha256(const void *data, size_t length, uint8_t digest[32]);

// Heap
void *malloc(size_t size);
void free(void *pointer);

#endif
