#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>

/* Kernel function prototypes */
void kernel_main(void);
void kernel_init(void);

/* GDT functions */
void gdt_init(void);

/* Memory management */
void *memset(void *dest, int val, size_t len);
void *memcpy(void *dest, const void *src, size_t len);
size_t strlen(const char *str);

/* String functions */
int strcmp(const char *str1, const char *str2);
int strncmp(const char *str1, const char *str2, size_t n);
char *strcpy(char *dest, const char *src);

/* System functions */
void panic(const char *message);
void hang(void);

/* Command processing */
void process_command(const char *command);
void print_prompt(void);

#endif /* KERNEL_H */