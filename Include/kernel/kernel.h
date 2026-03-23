#ifndef KERNEL_H
#define KERNEL_H

#include "lib/base.h"

/* -------------------------------------------------------------------------
 * Kernel entry point
 * mb2_info_phys: physical address of the multiboot2 info block (from GRUB).
 *                0 = not available (should not happen with GRUB).
 * ---------------------------------------------------------------------- */
void kernel_main(uint64_t mb2_info_phys);
void kernel_init(uint64_t mb2_info_phys);

/* System initialization */
void gdt_init(void);
void idt_init(void);
void paging_init(uint64_t reserved_phys_end);
void timer_init(uint32_t frequency);
void heap_init(void);

/* Memory management */
void *memset(void *dest, int val, size_t len);
void *memcpy(void *dest, const void *src, size_t len);
void *memmove(void *dest, const void *src, size_t len);
size_t strlen(const char *str);
int memcmp(const void *s1, const void *s2, size_t n);

/* Dynamic memory allocation */
void* kmalloc(size_t size);
void* kzalloc(size_t size);
void kfree(void* ptr);

/* String functions */
int strcmp(const char *str1, const char *str2);
int strncmp(const char *str1, const char *str2, size_t n);
char *strncpy(char *dest, const char *src, size_t n);
char *strstr(const char *haystack, const char *needle);

/* System functions */
void panic(const char *message);
void hang(void);
void runtime_init(void);

/* Utility functions */
void print_hex(uint64_t value);
void print_hex32(uint32_t value);
void print_dec(uint64_t value);

/* Checksum */

/* Printf-like function */
void kprintf(const char *format, ...);

/* Timer functions */
uint64_t timer_get_uptime_ms(void);

/* I/O port functions */
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t val);
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t val);
uint32_t inl(uint16_t port);
void outl(uint16_t port, uint32_t val);

#endif /* KERNEL_H */
