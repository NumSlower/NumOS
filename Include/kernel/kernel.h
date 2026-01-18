#ifndef KERNEL_H
#define KERNEL_H

#include "lib/base.h"

/* Kernel function prototypes */
void kernel_main(void);
void kernel_init(void);

/* System initialization */
void gdt_init(void);
void idt_init(void);
void paging_init(void);
void timer_init(uint32_t frequency);
void heap_init(void);

/* FAT32 filesystem initialization */
int fat32_init(void);
int fat32_mount(void);
void fat32_unmount(void);

/* Graphics mode initialization */
int graphics_init(void);
int graphics_set_mode(uint16_t width, uint16_t height, uint8_t bpp);
int graphics_switch_to_graphics(uint16_t width, uint16_t height, uint8_t bpp);
int graphics_switch_to_text(void);

/* Memory management */
void *memset(void *dest, int val, size_t len);
void *memcpy(void *dest, const void *src, size_t len);
size_t strlen(const char *str);
int memcmp(const void *s1, const void *s2, size_t n);

/* Dynamic memory allocation */
void* kmalloc(size_t size);
void* kzalloc(size_t size);
void* krealloc(void* ptr, size_t new_size);
void kfree(void* ptr);
void* kcalloc(size_t count, size_t size);
char* kstrdup(const char* str);

/* String functions */
int strcmp(const char *str1, const char *str2);
int strncmp(const char *str1, const char *str2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strstr(const char *haystack, const char *needle);

/* String conversion functions */
long strtol(const char *str, char **endptr, int base);

/* System functions */
void panic(const char *message);
void hang(void);

/* Command processing */
void process_command(const char *command);
void print_prompt(void);

/* Utility functions */
void print_hex(uint64_t value);
void print_hex32(uint32_t value);
void print_dec(uint64_t value);
void print_dec32(uint32_t value);
void print_memory(const void *ptr, size_t size);

/* Checksum */
uint32_t calculate_checksum(const void *data, size_t size);

/* Printf-like function */
void kprintf(const char *format, ...);

/* Timer functions */
uint64_t timer_get_ticks(void);
uint64_t timer_get_uptime_ms(void);
uint64_t timer_get_uptime_seconds(void);
void timer_sleep(uint32_t ms);

/* I/O port functions */
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t val);
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t val);
uint32_t inl(uint16_t port);
void outl(uint16_t port, uint32_t val);

#endif /* KERNEL_H */