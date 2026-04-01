#ifndef NUMOS_LIBC_H
#define NUMOS_LIBC_H

/*
 * libc.h - Small libc surface for NumOS user programs.
 *
 * This header exposes the memory, string, I/O, and thread helpers backed by
 * the current NumOS user runtime.
 */

#include "syscalls.h"

typedef intptr_t (*numos_thread_start_t)(void *arg);

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
int     thread_create(numos_thread_start_t start, void *arg);
int     thread_join(int tid, intptr_t *result);
void    thread_exit(intptr_t value);
int     thread_self(void);

#endif /* NUMOS_LIBC_H */
