#ifndef NUMOS_LIBC_H
#define NUMOS_LIBC_H

#include "syscalls.h"

void   *memset(void *dest, int value, size_t len);
void   *memcpy(void *dest, const void *src, size_t len);
void   *memmove(void *dest, const void *src, size_t len);
int     memcmp(const void *a, const void *b, size_t len);
size_t  strlen(const char *str);
int     strcmp(const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t len);
char   *strncpy(char *dest, const char *src, size_t len);
ssize_t write(int fd, const void *buf, size_t len);
int     puts(const char *str);
void    exit(int status);

#endif /* NUMOS_LIBC_H */
