#ifndef NUMOS_LIBC_H
#define NUMOS_LIBC_H

/*
 * libc.h - Small libc surface for NumOS user programs.
 *
 * This header exposes the memory, string, I/O, parsing, and thread helpers
 * backed by the current NumOS user runtime.
 */

#include "syscalls.h"

typedef intptr_t (*numos_thread_start_t)(void *arg);
typedef __PTRDIFF_TYPE__ ptrdiff_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255u
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 65535u
#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  2147483647
#define UINT32_MAX 4294967295u
#define INT64_MIN  (-9223372036854775807LL - 1)
#define INT64_MAX  9223372036854775807LL
#define UINT64_MAX 18446744073709551615ULL

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

void   *memset(void *dest, int value, size_t len);
void   *memcpy(void *dest, const void *src, size_t len);
void   *memmove(void *dest, const void *src, size_t len);
int     memcmp(const void *a, const void *b, size_t len);
size_t  strlen(const char *str);
int     strcmp(const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t len);
char   *strcpy(char *dest, const char *src);
char   *strcat(char *dest, const char *src);
char   *strncpy(char *dest, const char *src, size_t len);
char   *strchr(const char *str, int ch);
char   *strrchr(const char *str, int ch);
char   *strstr(const char *haystack, const char *needle);
char   *strerror(int errnum);

int     isalnum(int ch);
int     isalpha(int ch);
int     isdigit(int ch);
int     islower(int ch);
int     isprint(int ch);
int     isspace(int ch);
int     isupper(int ch);
int     isxdigit(int ch);
int     tolower(int ch);
int     toupper(int ch);

#define RAND_MAX 2147483647

int     abs(int value);
int     atoi(const char *nptr);
void   *malloc(size_t size);
void    free(void *ptr);
void   *realloc(void *ptr, size_t size);
void    abort(void);
char   *getenv(const char *name);
int     rand(void);
void    srand(unsigned int seed);
double  strtod(const char *nptr, char **endptr);
long    strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);

ssize_t write(int fd, const void *buf, size_t len);
int     puts(const char *str);
int     isatty(int fd);
void    exit(int status);

int     thread_create(numos_thread_start_t start, void *arg);
int     thread_join(int tid, intptr_t *result);
void    thread_exit(intptr_t value);
int     thread_self(void);

#endif /* NUMOS_LIBC_H */
