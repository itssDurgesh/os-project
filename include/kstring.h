/* Memory and string helpers. The kernel has no C library, so these live here. */

#ifndef KSTRING_H
#define KSTRING_H

#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t count);
void *memmove(void *dest, const void *src, size_t count);
void *memset(void *dest, int value, size_t count);
int memcmp(const void *a, const void *b, size_t count);
size_t strlen(const char *text);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t count);
void strlcpy(char *dest, const char *src, size_t size);

#endif
