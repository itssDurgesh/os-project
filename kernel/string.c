/* Memory and string helpers. GCC may also call the mem* functions by itself. */

#include "kstring.h"

void *memcpy(void *dest, const void *src, size_t count)
{
    unsigned char *to = dest;
    const unsigned char *from = src;

    while (count-- > 0)
        *to++ = *from++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t count)
{
    unsigned char *to = dest;
    const unsigned char *from = src;

    if (to < from)
        return memcpy(dest, src, count);

    while (count-- > 0)
        to[count] = from[count];
    return dest;
}

void *memset(void *dest, int value, size_t count)
{
    unsigned char *to = dest;

    while (count-- > 0)
        *to++ = (unsigned char)value;
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

void strlcpy(char *dest, const char *src, size_t size)
{
    size_t i = 0;

    if (size == 0)
        return;

    for (; i < size - 1 && src[i] != '\0'; i++)
        dest[i] = src[i];
    dest[i] = '\0';
}
