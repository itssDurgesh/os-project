/* User accounts, from /etc/passwd (name:x:uid:gid:home:shell) and /etc/shadow
 * (name:salt:hash). */

#include "libc.h"

#define FILE_BUFFER 2048
#define LINE_FIELDS 6
#define SHADOW_FIELDS 3
#define HASH_BYTES 32

static char buffer[FILE_BUFFER];

static bool load_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    int length;

    if (fd < 0)
        return false;

    length = read(fd, buffer, FILE_BUFFER - 1);
    close(fd);
    if (length < 0)
        return false;

    buffer[length] = '\0';
    return true;
}

/* Cuts one line into fields at each colon, and returns the start of the next. */
static char *split_line(char *line, char *fields[], int count)
{
    char *cursor = line;
    int field = 0;

    fields[field++] = cursor;
    for (; *cursor != '\0' && *cursor != '\n'; cursor++) {
        if (*cursor == ':' && field < count) {
            *cursor = '\0';
            fields[field++] = cursor + 1;
        }
    }

    while (field < count)
        fields[field++] = cursor;

    if (*cursor == '\n')
        *cursor++ = '\0';
    return cursor;
}

static bool find_user(const char *name, int uid, struct user *result)
{
    char *line = buffer;

    if (!load_file("/etc/passwd"))
        return false;

    while (*line != '\0') {
        char *fields[LINE_FIELDS];

        line = split_line(line, fields, LINE_FIELDS);

        if ((name != NULL && strcmp(fields[0], name) == 0) ||
            (name == NULL && atoi(fields[2]) == uid)) {
            strcpy(result->name, fields[0]);
            result->uid = atoi(fields[2]);
            result->gid = atoi(fields[3]);
            strcpy(result->home, fields[4]);
            strcpy(result->shell, fields[5]);
            return true;
        }
    }
    return false;
}

bool user_by_name(const char *name, struct user *result)
{
    return find_user(name, -1, result);
}

bool user_by_uid(int uid, struct user *result)
{
    return find_user(NULL, uid, result);
}

static int hex_value(char c)
{
    return (c >= 'a') ? c - 'a' + 10 : c - '0';
}

bool user_check_password(const char *name, const char *password)
{
    static char salted[128];
    uint8_t digest[HASH_BYTES];
    char *line = buffer;

    if (!load_file("/etc/shadow"))
        return false;

    while (*line != '\0') {
        char *fields[SHADOW_FIELDS];

        line = split_line(line, fields, SHADOW_FIELDS);
        if (strcmp(fields[0], name) != 0)
            continue;

        if (strlen(fields[1]) + strlen(password) >= sizeof(salted) ||
            strlen(fields[2]) != HASH_BYTES * 2)
            return false;

        strcpy(salted, fields[1]);
        strcpy(salted + strlen(fields[1]), password);
        sha256(salted, strlen(salted), digest);

        for (int i = 0; i < HASH_BYTES; i++) {
            int stored = hex_value(fields[2][i * 2]) * 16 +
                         hex_value(fields[2][i * 2 + 1]);

            if (stored != digest[i])
                return false;
        }
        return true;
    }
    return false;
}
