#ifndef _TYPES_H
#define _TYPES_H

#include "lib/stdint.h"
#include "lib/stdbool.h"
#include "lib/stddef.h"

// ssize_t is a signed version of size_t
typedef long ssize_t;

// off_t is typically used for file offsets
typedef long off_t;

// Define other basic kernel-safe types if needed
typedef uint64_t ino_t;   // inode number type
typedef uint32_t mode_t;  // permissions
typedef uint32_t uid_t;   // user id
typedef uint32_t gid_t;   // group id

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

typedef u64 phys_addr_t;
typedef u64 virt_addr_t;

typedef u8  byte;

#ifndef NULL
#define NULL ((void*)0)
#endif

#endif
