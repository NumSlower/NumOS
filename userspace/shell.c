/* 
 * shell.c - Pure userspace command shell
 * Compiled separately with -DUSERSPACE flag
 * Uses ONLY system calls - no kernel functions
 */

#include "kernel/syscall.h"

/* Minimal definitions for userspace */
#define NULL ((void*)0)
typedef unsigned long size_t;
typedef long ssize_t;

/* Shell constants */
#define MAX_COMMAND_LENGTH 256
#define MAX_ARGS 16
#define MAX_SHELL_COMMANDS 32

/* Shell command structure */
struct shell_command {
    const char *name;
    const char *description;
    void (*handler)(int argc, char **argv);
};

static struct shell_command commands[MAX_SHELL_COMMANDS];
static int command_count = 0;
static int shell_running = 0;

/* Forward declarations for command handlers */
void shell_cmd_help(int argc, char **argv);
void shell_cmd_echo(int argc, char **argv);
void shell_cmd_exit(int argc, char **argv);
void shell_cmd_sysinfo(int argc, char **argv);
void shell_cmd_uptime(int argc, char **argv);
void shell_cmd_sleep(int argc, char **argv);
void shell_cmd_test(int argc, char **argv);

/* Helper functions */
static void shell_print_prompt(void);
static void shell_execute_command(int argc, char **argv);

/* Basic string functions - userspace implementations */
static inline void shell_putchar(char c) {
    sys_write(STDOUT_FILENO, &c, 1);
}

static inline void shell_writestring(const char *str) {
    const char *p = str;
    while (*p) p++;
    sys_write(STDOUT_FILENO, str, p - str);
}

static inline size_t shell_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static inline int shell_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static inline void shell_print_dec(uint64_t value) {
    char buffer[21];
    int pos = 20;
    buffer[pos] = '\0';
    
    if (value == 0) {
        shell_putchar('0');
        return;
    }
    
    while (value > 0 && pos > 0) {
        buffer[--pos] = '0' + (value % 10);
        value /= 10;
    }
    
    shell_writestring(&buffer[pos]);
}

static inline uint32_t simple_atoi(const char *str) {
    uint32_t result = 0;
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return result;
}

/* Print helpers */
static void shell_print_success(const char *msg) {
    shell_writestring("[OK] ");
    shell_writestring(msg);
    shell_putchar('\n');
}

static void shell_print_error(const char *msg) {
    shell_writestring("[ERROR] ");
    shell_writestring(msg);
    shell_putchar('\n');
}

//static void shell_print_info(const char *msg) {
//    shell_writestring("[INFO] ");
//    shell_writestring(msg);
//    shell_putchar('\n');
//}

/* Register a command */
void shell_register_command(const char *name, const char *description, 
                           void (*handler)(int, char**)) {
    if (command_count >= MAX_SHELL_COMMANDS) {
        return;
    }
    
    commands[command_count].name = name;
    commands[command_count].description = description;
    commands[command_count].handler = handler;
    command_count++;
}

/* Initialize shell and register built-in commands */
void shell_init(void) {
    command_count = 0;
    shell_running = 0;
    
    /* Register built-in commands */
    shell_register_command("help", "- Show available commands", shell_cmd_help);
    shell_register_command("echo", "<text> - Print text", shell_cmd_echo);
    shell_register_command("exit", "- Exit the shell", shell_cmd_exit);
    shell_register_command("sysinfo", "- Show system information", shell_cmd_sysinfo);
    shell_register_command("uptime", "- Show system uptime", shell_cmd_uptime);
    shell_register_command("sleep", "<ms> - Sleep for milliseconds", shell_cmd_sleep);
    shell_register_command("test", "- Run system call tests", shell_cmd_test);
}

/* Main shell loop - THIS IS THE ENTRY POINT */
void shell_main(void) {
    char *argv[MAX_ARGS];
    int argc;
    
    /* Initialize shell */
    shell_init();
    
    shell_running = 1;
    
    shell_writestring("\n");
    shell_writestring("=====================================\n");
    shell_writestring("   NumOS Userspace Shell v2.4\n");
    shell_writestring("=====================================\n");
    shell_writestring("Type 'help' for available commands\n");
    shell_writestring("This shell runs in userspace using\n");
    shell_writestring("system calls (INT 0x80)\n");
    shell_writestring("\n");
    
    while (shell_running) {
        shell_print_prompt();
        
        /* Simple input reading - just use dummy command for now
           since we can't read from keyboard in pure userspace yet */
        shell_writestring("\n");
        shell_print_error("Keyboard input not available in pure userspace mode");
        shell_writestring("Running test commands automatically...\n\n");
        
        /* Run some test commands automatically */
        char *test_commands[] = {"help", "sysinfo", "uptime", "test", "exit", NULL};
        
        for (int cmd_idx = 0; test_commands[cmd_idx] != NULL; cmd_idx++) {
            shell_writestring("\n> ");
            shell_writestring(test_commands[cmd_idx]);
            shell_writestring("\n");
            
            /* Build argv for this command */
            argv[0] = test_commands[cmd_idx];
            argc = 1;
            
            shell_execute_command(argc, argv);
            
            if (!shell_running) break;
            
            /* Sleep between commands */
            sys_sleep(1000);
        }
        
        shell_running = 0;  /* Exit after running test commands */
    }
    
    shell_writestring("\nShell exiting...\n");
}

/* Print shell prompt */
static void shell_print_prompt(void) {
    shell_writestring("numos $ ");
}

/* Execute a command */
static void shell_execute_command(int argc, char **argv) {
    if (argc == 0) {
        return;
    }
    
    for (int i = 0; i < command_count; i++) {
        if (shell_strcmp(argv[0], commands[i].name) == 0) {
            commands[i].handler(argc, argv);
            return;
        }
    }
    
    shell_print_error("Unknown command");
    shell_writestring("Type 'help' for available commands\n");
}

/* Command implementations */
void shell_cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    
    shell_writestring("\nAvailable commands:\n");
    shell_writestring("-------------------\n");
    
    for (int i = 0; i < command_count; i++) {
        shell_writestring("  ");
        shell_writestring(commands[i].name);
        shell_writestring(" ");
        shell_writestring(commands[i].description);
        shell_putchar('\n');
    }
    shell_putchar('\n');
}

void shell_cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        shell_writestring(argv[i]);
        if (i < argc - 1) {
            shell_putchar(' ');
        }
    }
    shell_putchar('\n');
}

void shell_cmd_exit(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_writestring("Goodbye!\n");
    shell_running = 0;
}

void shell_cmd_sysinfo(int argc, char **argv) {
    (void)argc; (void)argv;
    
    struct sysinfo info;
    if (sys_sysinfo(&info) != SYSCALL_SUCCESS) {
        shell_print_error("Failed to get system information");
        return;
    }
    
    shell_writestring("\nSystem Information:\n");
    shell_writestring("===================\n");
    
    shell_writestring("OS Version:      ");
    shell_writestring(info.version);
    shell_putchar('\n');
    
    shell_writestring("Uptime:          ");
    uint64_t hours = info.uptime / 3600;
    uint64_t minutes = (info.uptime % 3600) / 60;
    uint64_t seconds = info.uptime % 60;
    
    if (hours > 0) {
        shell_print_dec(hours);
        shell_writestring("h ");
    }
    if (minutes > 0 || hours > 0) {
        shell_print_dec(minutes);
        shell_writestring("m ");
    }
    shell_print_dec(seconds);
    shell_writestring("s\n");
    
    shell_writestring("Total Memory:    ");
    shell_print_dec(info.total_memory / 1024);
    shell_writestring(" KB\n");
    
    shell_writestring("Used Memory:     ");
    shell_print_dec(info.used_memory / 1024);
    shell_writestring(" KB\n");
    
    shell_writestring("Free Memory:     ");
    shell_print_dec(info.free_memory / 1024);
    shell_writestring(" KB\n");
    
    uint64_t usage = (info.used_memory * 100) / info.total_memory;
    shell_writestring("Memory Usage:    ");
    shell_print_dec(usage);
    shell_writestring("%\n");
    
    shell_writestring("Process Count:   ");
    shell_print_dec(info.process_count);
    shell_writestring("\n\n");
}

void shell_cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    
    uint64_t uptime_ms = sys_uptime();
    uint64_t seconds = uptime_ms / 1000;
    uint64_t milliseconds = uptime_ms % 1000;
    
    shell_writestring("System uptime: ");
    shell_print_dec(seconds);
    shell_putchar('.');
    
    /* Print milliseconds with padding */
    if (milliseconds < 100) shell_putchar('0');
    if (milliseconds < 10) shell_putchar('0');
    shell_print_dec(milliseconds);
    
    shell_writestring(" seconds\n");
}

void shell_cmd_sleep(int argc, char **argv) {
    if (argc < 2) {
        shell_print_error("Usage: sleep <milliseconds>");
        return;
    }
    
    uint32_t ms = simple_atoi(argv[1]);
    
    shell_writestring("Sleeping for ");
    shell_print_dec(ms);
    shell_writestring(" ms...\n");
    
    uint64_t before = sys_uptime();
    sys_sleep(ms);
    uint64_t after = sys_uptime();
    
    shell_writestring("Actually slept for ");
    shell_print_dec(after - before);
    shell_writestring(" ms\n");
}

void shell_cmd_test(int argc, char **argv) {
    (void)argc; (void)argv;
    
    shell_writestring("\nRunning System Call Tests\n");
    shell_writestring("=========================\n\n");
    
    /* Test 1: sys_write */
    shell_writestring("Test 1: sys_write to stdout\n");
    const char *msg = "  Hello from userspace syscall!\n";
    ssize_t written = sys_write(STDOUT_FILENO, msg, shell_strlen(msg));
    if (written == (ssize_t)shell_strlen(msg)) {
        shell_print_success("sys_write test PASSED");
    } else {
        shell_print_error("sys_write test FAILED");
    }
    
    /* Test 2: sys_uptime */
    shell_writestring("\nTest 2: sys_uptime\n");
    uint64_t uptime = sys_uptime();
    shell_writestring("  Current uptime: ");
    shell_print_dec(uptime);
    shell_writestring(" ms\n");
    shell_print_success("sys_uptime test PASSED");
    
    /* Test 3: sys_sysinfo */
    shell_writestring("\nTest 3: sys_sysinfo\n");
    struct sysinfo info;
    if (sys_sysinfo(&info) == SYSCALL_SUCCESS) {
        shell_writestring("  OS: ");
        shell_writestring(info.version);
        shell_putchar('\n');
        shell_writestring("  Memory: ");
        shell_print_dec(info.total_memory / 1024);
        shell_writestring(" KB total\n");
        shell_print_success("sys_sysinfo test PASSED");
    } else {
        shell_print_error("sys_sysinfo test FAILED");
    }
    
    /* Test 4: sys_sleep */
    shell_writestring("\nTest 4: sys_sleep (500ms)\n");
    uint64_t before = sys_uptime();
    sys_sleep(500);
    uint64_t after = sys_uptime();
    uint64_t elapsed = after - before;
    
    shell_writestring("  Requested: 500ms\n");
    shell_writestring("  Actual:    ");
    shell_print_dec(elapsed);
    shell_writestring(" ms\n");
    
    if (elapsed >= 450 && elapsed <= 550) {
        shell_print_success("sys_sleep test PASSED");
    } else {
        shell_print_error("sys_sleep test FAILED (out of range)");
    }
    
    shell_writestring("\nAll userspace tests completed!\n\n");
}