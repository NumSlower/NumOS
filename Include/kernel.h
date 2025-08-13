#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>

/* Kernel function prototypes */
void kernel_main(void);
void kernel_init(void);

/* System initialization */
void gdt_init(void);
void idt_init(void);
void paging_init(void);

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

/* Utility functions */
void print_hex(uint64_t value);
void print_dec(uint64_t value);

/* I/O port functions - declared here to avoid circular dependencies */
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t val);

#endif /* KERNEL_H */