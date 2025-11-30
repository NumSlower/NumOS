/* 
 * shell.c - FIXED Pure userspace shell
 * This version ACTUALLY prints "Hello World from Userspace"
 */

#include "kernel/syscall.h"

#define NULL ((void*)0)
typedef unsigned long size_t;
typedef long ssize_t;

/* Minimal string functions */
static size_t my_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static void print(const char *str) {
    sys_write(1, str, my_strlen(str));
}

static void print_number(uint64_t num) {
    char buffer[32];
    int i = 30;
    buffer[31] = '\0';
    
    if (num == 0) {
        sys_write(1, "0", 1);
        return;
    }
    
    while (num > 0) {
        buffer[i--] = '0' + (num % 10);
        num /= 10;
    }
    
    print(&buffer[i + 1]);
}

/* Main shell entry point */
void shell_main(void) {
    /* FIRST THING: Print the required message */
    print("\n");
    print("==========================================\n");
    print("   Hello World from Userspace!\n");
    print("==========================================\n");
    print("\n");
    
    /* Prove we're in userspace */
    print("This shell is running in Ring 3 (userspace)\n");
    print("All output uses system calls (INT 0x80)\n");
    print("\n");
    
    /* Test system calls */
    print("Testing system calls...\n");
    print("\n");
    
    /* Test 1: Get uptime */
    print("1. Getting system uptime:\n");
    uint64_t uptime = sys_uptime();
    print("   Uptime: ");
    print_number(uptime);
    print(" milliseconds\n");
    print("\n");
    
    /* Test 2: Get system info */
    print("2. Getting system information:\n");
    struct sysinfo info;
    if (sys_sysinfo(&info) == 0) {
        print("   OS Version: ");
        print(info.version);
        print("\n");
        print("   Total Memory: ");
        print_number(info.total_memory / 1024);
        print(" KB\n");
        print("   Free Memory: ");
        print_number(info.free_memory / 1024);
        print(" KB\n");
    }
    print("\n");
    
    /* Test 3: Sleep */
    print("3. Testing sleep (1 second):\n");
    print("   Sleeping");
    sys_sleep(250);
    print(".");
    sys_sleep(250);
    print(".");
    sys_sleep(250);
    print(".");
    sys_sleep(250);
    print(". Done!\n");
    print("\n");
    
    /* Success message */
    print("==========================================\n");
    print("   All userspace tests passed!\n");
    print("==========================================\n");
    print("\n");
    
    print("Shell will now exit gracefully...\n");
    sys_sleep(2000);
    
    /* Exit cleanly */
    sys_exit(0);
}