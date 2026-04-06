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

/* -------------------------------------------------------------------------
 * Fundamental fixed-width integer types.
 *
 * We use the GCC/Clang predefined macros rather than pulling in any standard
 * header so this file remains self-contained for freestanding (NumOS) builds.
 * Every supported toolchain defines these macros correctly for the target.
 * ---------------------------------------------------------------------- */
typedef __INT8_TYPE__    int8_t;
typedef __INT16_TYPE__   int16_t;
typedef __INT32_TYPE__   int32_t;
typedef __INT64_TYPE__   int64_t;
typedef __UINT8_TYPE__   uint8_t;
typedef __UINT16_TYPE__  uint16_t;
typedef __UINT32_TYPE__  uint32_t;
typedef __UINT64_TYPE__  uint64_t;
typedef __INTPTR_TYPE__  intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __SIZE_TYPE__    size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;

/* ssize_t: signed counterpart of size_t.  On all NumOS targets this is a
 * 64-bit signed integer; adjust if a 32-bit-only target is ever added. */
typedef int64_t ssize_t;

/* -------------------------------------------------------------------------
 * Convenience limits.
 * ---------------------------------------------------------------------- */
#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255u
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 65535u
#define INT32_MIN  (-2147483648)
#define INT32_MAX  2147483647
#define UINT32_MAX 4294967295u
#define INT64_MIN  (-9223372036854775807LL - 1)
#define INT64_MAX  9223372036854775807LL
#define UINT64_MAX 18446744073709551615ULL

/* NULL */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* -------------------------------------------------------------------------
 * File-descriptor constants.
 * ---------------------------------------------------------------------- */
#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

/* FAT32 open flags come from syscalls.h. */

/* -------------------------------------------------------------------------
 * Thread support.
 *
 * Threads in NumOS are kernel-managed; thread_create issues a syscall that
 * spawns a new thread in the same address space.
 * ---------------------------------------------------------------------- */

/* Function type for the thread entry point. */
typedef intptr_t (*numos_thread_start_t)(void *arg);

/*
 * thread_create - spawn a new thread.
 *
 * start - the thread entry point.
 * arg   - an arbitrary pointer passed to start().
 *
 * Returns a non-negative thread ID on success, or a negative error code.
 */
int thread_create(numos_thread_start_t start, void *arg);

/*
 * thread_join - wait for a thread to finish.
 *
 * tid    - the thread ID returned by thread_create.
 * result - if non-NULL, receives the value returned by the thread's entry
 *          point (or passed to thread_exit).
 *
 * Returns 0 on success or a negative error code on failure.
 */
int thread_join(int tid, intptr_t *result);

/*
 * thread_exit - terminate the calling thread.
 *
 * value - the exit value made available to thread_join callers.
 *
 * Does not return.
 */
void thread_exit(intptr_t value);

/*
 * thread_self - return the calling thread's ID.
 */
int thread_self(void);

/* -------------------------------------------------------------------------
 * Memory and string functions.
 *
 * These are provided by the NumOS user runtime; their semantics match the
 * corresponding ISO C standard-library functions exactly.
 * ---------------------------------------------------------------------- */
void   *memset(void *dest, int value, size_t len);
void   *memcpy(void *dest, const void *src, size_t len);
void   *memmove(void *dest, const void *src, size_t len);
int     memcmp(const void *a, const void *b, size_t len);
size_t  strlen(const char *str);
int     strcmp(const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t len);
char   *strncpy(char *dest, const char *src, size_t len);

/* -------------------------------------------------------------------------
 * Higher-level I/O helpers.
 * ---------------------------------------------------------------------- */
/* write() - POSIX-compatible write; thin wrapper over sys_write. */
ssize_t write(int fd, const void *buf, size_t len);

/* puts() - write str followed by a newline to FD_STDOUT. */
int puts(const char *str);

/* exit() - terminate the process with the given status code. */
void exit(int status);

#endif /* NUMOS_LIBC_H */
