/* The small C library for user programs. */

#include "libc.h"

#define PRINT_BUFFER 512

/* System calls: number in EAX, arguments in EBX, ECX and EDX. */

static inline int syscall3(int number, uint32_t a, uint32_t b, uint32_t c)
{
    int result;

    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "a"(number), "b"(a), "c"(b), "d"(c)
                      : "memory");
    return result;
}

int fork(void) { return syscall3(SYS_FORK, 0, 0, 0); }
int getpid(void) { return syscall3(SYS_GETPID, 0, 0, 0); }
int getuid(void) { return syscall3(SYS_GETUID, 0, 0, 0); }
int getgid(void) { return syscall3(SYS_GETGID, 0, 0, 0); }
int wait(int *status) { return syscall3(SYS_WAIT, (uint32_t)status, 0, 0); }
int sleep(uint32_t milliseconds) { return syscall3(SYS_SLEEP, milliseconds, 0, 0); }
int close(int fd) { return syscall3(SYS_CLOSE, fd, 0, 0); }
int unlink(const char *path) { return syscall3(SYS_UNLINK, (uint32_t)path, 0, 0); }
int rmdir(const char *path) { return syscall3(SYS_RMDIR, (uint32_t)path, 0, 0); }
int chdir(const char *path) { return syscall3(SYS_CHDIR, (uint32_t)path, 0, 0); }
int pipe(int fds[2]) { return syscall3(SYS_PIPE, (uint32_t)fds, 0, 0); }
int dup2(int old_fd, int new_fd) { return syscall3(SYS_DUP2, old_fd, new_fd, 0); }

int setuid(int uid) { return syscall3(SYS_SETUID, uid, 0, 0); }
int setgid(int gid) { return syscall3(SYS_SETGID, gid, 0, 0); }
int set_echo(bool enabled) { return syscall3(SYS_SET_ECHO, enabled, 0, 0); }
int chmod(const char *path, int mode) { return syscall3(SYS_CHMOD, (uint32_t)path, mode, 0); }

int procinfo(int index, struct procinfo *info)
{
    return syscall3(SYS_PROCINFO, index, (uint32_t)info, 0);
}

int exec(const char *path, char *const argv[])
{
    return syscall3(SYS_EXEC, (uint32_t)path, (uint32_t)argv, 0);
}

void exit(int status)
{
    syscall3(SYS_EXIT, status, 0, 0);
    for (;;)
        ;
}

void *sbrk(int increment)
{
    return (void *)syscall3(SYS_SBRK, increment, 0, 0);
}

int open(const char *path, int flags, ...)
{
    va_list args;
    int mode;

    va_start(args, flags);
    mode = (flags & O_CREAT) ? va_arg(args, int) : 0;
    va_end(args);
    return syscall3(SYS_OPEN, (uint32_t)path, flags, mode);
}

int read(int fd, void *buffer, uint32_t count)
{
    return syscall3(SYS_READ, fd, (uint32_t)buffer, count);
}

int write(int fd, const void *buffer, uint32_t count)
{
    return syscall3(SYS_WRITE, fd, (uint32_t)buffer, count);
}

int seek(int fd, int offset, int whence)
{
    return syscall3(SYS_SEEK, fd, offset, whence);
}

int stat(const char *path, struct stat *result)
{
    return syscall3(SYS_STAT, (uint32_t)path, (uint32_t)result, 0);
}

int mkdir(const char *path, int mode)
{
    return syscall3(SYS_MKDIR, (uint32_t)path, mode, 0);
}

int readdir(int fd, struct dirent *entry)
{
    return syscall3(SYS_READDIR, fd, (uint32_t)entry, 0);
}

int getcwd(char *buffer, uint32_t size)
{
    return syscall3(SYS_GETCWD, (uint32_t)buffer, size, 0);
}

/* Strings and memory */

size_t strlen(const char *text)
{
    size_t length = 0;

    while (text[length] != '\0')
        length++;
    return length;
}

int strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (a[i] != b[i] || a[i] == '\0')
            return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

char *strcpy(char *dest, const char *src)
{
    char *start = dest;

    while ((*dest++ = *src++) != '\0')
        ;
    return start;
}

char *strchr(const char *text, int c)
{
    for (; *text != '\0'; text++) {
        if (*text == (char)c)
            return (char *)text;
    }
    return NULL;
}

void *memset(void *dest, int value, size_t count)
{
    unsigned char *to = dest;

    while (count-- > 0)
        *to++ = (unsigned char)value;
    return dest;
}

void *memcpy(void *dest, const void *src, size_t count)
{
    unsigned char *to = dest;
    const unsigned char *from = src;

    while (count-- > 0)
        *to++ = *from++;
    return dest;
}

int memcmp(const void *a, const void *b, size_t count)
{
    const unsigned char *left = a;
    const unsigned char *right = b;

    for (size_t i = 0; i < count; i++) {
        if (left[i] != right[i])
            return left[i] - right[i];
    }
    return 0;
}

int atoi(const char *text)
{
    int value = 0;

    while (*text >= '0' && *text <= '9')
        value = value * 10 + (*text++ - '0');
    return value;
}

/* Output */

struct output {
    int fd;
    char buffer[PRINT_BUFFER];
    uint32_t length;
};

static void flush(struct output *out)
{
    if (out->length > 0) {
        write(out->fd, out->buffer, out->length);
        out->length = 0;
    }
}

static void put_char(struct output *out, char c)
{
    if (out->length == PRINT_BUFFER)
        flush(out);
    out->buffer[out->length++] = c;
}

static void put_string(struct output *out, const char *text)
{
    for (; *text != '\0'; text++)
        put_char(out, *text);
}

static void put_number(struct output *out, uint32_t value, uint32_t base)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[16];
    int length = 0;

    do {
        buffer[length++] = digits[value % base];
        value /= base;
    } while (value != 0);

    while (length > 0)
        put_char(out, buffer[--length]);
}

static void format_to(int fd, const char *format, va_list args)
{
    struct output out;

    out.fd = fd;
    out.length = 0;

    for (; *format != '\0'; format++) {
        if (*format != '%') {
            put_char(&out, *format);
            continue;
        }

        format++;
        switch (*format) {
        case 'd': {
            int value = va_arg(args, int);

            if (value < 0) {
                put_char(&out, '-');
                put_number(&out, -(uint32_t)value, 10);
            } else {
                put_number(&out, value, 10);
            }
            break;
        }
        case 'u':
            put_number(&out, va_arg(args, uint32_t), 10);
            break;
        case 'x':
            put_number(&out, va_arg(args, uint32_t), 16);
            break;
        case 's':
            put_string(&out, va_arg(args, const char *));
            break;
        case 'c':
            put_char(&out, (char)va_arg(args, int));
            break;
        case '%':
            put_char(&out, '%');
            break;
        default:
            put_char(&out, '%');
            put_char(&out, *format);
            break;
        }
    }

    flush(&out);
}

int printf(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    format_to(STDOUT, format, args);
    va_end(args);
    return 0;
}

int fprintf(int fd, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    format_to(fd, format, args);
    va_end(args);
    return 0;
}

const char *error_text(int error)
{
    switch (-error) {
    case ENOENT:    return "no such file or directory";
    case EACCES:    return "permission denied";
    case EEXIST:    return "already exists";
    case ENOTDIR:   return "not a directory";
    case EISDIR:    return "is a directory";
    case ENOTEMPTY: return "directory isn't empty";
    case ENOSPC:    return "disk full";
    case ENOMEM:    return "out of memory";
    case ENOEXEC:   return "not a valid program";
    case EBADF:     return "bad file descriptor";
    case EINVAL:    return "invalid argument";
    default:        return "error";
    }
}

/* Heap: a simple first-fit list of blocks, grown with sbrk(). */

#define HEAP_GROW_SIZE 16384
#define ALIGNMENT 8

struct block {
    size_t size;
    bool free;
    struct block *next;
};

static struct block *heap_first;
static struct block *heap_last;

static size_t round_up(size_t size)
{
    return (size + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1);
}

static struct block *grow_heap(size_t size)
{
    size_t total = sizeof(struct block) + size;
    struct block *block;

    if (total < HEAP_GROW_SIZE)
        total = HEAP_GROW_SIZE;

    block = sbrk((int)total);
    if ((int)block < 0 && (int)block > -4096)
        return NULL;

    block->size = total - sizeof(struct block);
    block->free = true;
    block->next = NULL;

    if (heap_last != NULL)
        heap_last->next = block;
    else
        heap_first = block;
    heap_last = block;
    return block;
}

void *malloc(size_t size)
{
    struct block *block;

    size = round_up(size);

    for (block = heap_first; block != NULL; block = block->next) {
        if (block->free && block->size >= size)
            break;
    }

    if (block == NULL) {
        block = grow_heap(size);
        if (block == NULL)
            return NULL;
    }

    /* Split off what's left, if it's big enough to be useful. */
    if (block->size >= size + sizeof(struct block) + ALIGNMENT) {
        struct block *rest = (struct block *)((char *)(block + 1) + size);

        rest->size = block->size - size - sizeof(struct block);
        rest->free = true;
        rest->next = block->next;
        block->next = rest;
        block->size = size;
        if (heap_last == block)
            heap_last = rest;
    }

    block->free = false;
    return block + 1;
}

void free(void *pointer)
{
    struct block *block;

    if (pointer == NULL)
        return;

    block = (struct block *)pointer - 1;
    block->free = true;

    /* Join neighbouring free blocks, so memory doesn't break into small pieces. */
    for (block = heap_first; block != NULL && block->next != NULL;) {
        struct block *next = block->next;
        char *end = (char *)(block + 1) + block->size;

        if (block->free && next->free && end == (char *)next) {
            block->size += sizeof(struct block) + next->size;
            block->next = next->next;
            if (heap_last == next)
                heap_last = block;
        } else {
            block = next;
        }
    }
}
