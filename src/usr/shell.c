#include "usr/shell.h"
#include "kernel.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "cpu/heap.h"
#include "cpu/paging.h"
#include "drivers/timer.h"
#include "fs/fat32.h"
#include "drivers/disk.h"

/* Shell state */
static struct shell_state shell_state = {0};
static struct shell_stats shell_stats = {0};

/* Command registry */
#define MAX_COMMANDS 64
static struct shell_command command_registry[MAX_COMMANDS];
static int registered_commands = 0;

/* Helper Functions */
static int shell_parse_hex(const char *hex_str, uint64_t *value);
static int shell_parse_dec(const char *dec_str, uint32_t *value);
static void shell_register_builtin_commands(void);

void shell_init(void) {
    /* Allocate command buffer */
    shell_state.buffer = (char*)kmalloc(SHELL_BUFFER_SIZE);
    if (!shell_state.buffer) {
        panic("Failed to allocate shell buffer");
        return;
    }
    
    shell_state.buffer_size = SHELL_BUFFER_SIZE;
    shell_state.running = 1;
    shell_state.command_count = 0;
    shell_state.start_time = timer_get_uptime_ms();
    
    /* Register built-in commands */
    shell_register_builtin_commands();
    
    /* Initialize stats */
    shell_stats.commands_executed = 0;
    shell_stats.errors = 0;
    shell_stats.successful_commands = 0;
}

void shell_run(void) {
    if (!shell_state.buffer) {
        shell_init();
    }
    
    shell_print_welcome();
    
    /* Main shell loop */
    while (shell_state.running) {
        shell_print_prompt();
        keyboard_read_line(shell_state.buffer, shell_state.buffer_size);
        shell_process_command(shell_state.buffer);
        shell_state.command_count++;
    }
}

void shell_shutdown(void) {
    if (shell_state.buffer) {
        kfree(shell_state.buffer);
        shell_state.buffer = NULL;
    }
    shell_state.running = 0;
}

void shell_process_command(const char *command_line) {
    if (!command_line || strlen(command_line) == 0) {
        return;
    }
    
    int argc;
    char **argv;
    shell_parse_command(command_line, &argc, &argv);
    
    if (argc == 0) {
        shell_free_args(argc, argv);
        return;
    }
    
    /* Find and execute command */
    struct shell_command *cmd = shell_find_command(argv[0]);
    if (cmd) {
        /* Check argument count */
        if (argc - 1 < cmd->min_args) {
            shell_print_error("Too few arguments for command");
            vga_writestring("Usage: ");
            vga_writestring(cmd->name);
            vga_writestring(" ");
            vga_writestring(cmd->description);
            vga_putchar('\n');
            shell_stats.errors++;
        } else if (cmd->max_args >= 0 && argc - 1 > cmd->max_args) {
            shell_print_error("Too many arguments for command");
            shell_stats.errors++;
        } else {
            /* Execute command */
            cmd->handler(argc, argv);
            shell_stats.commands_executed++;
            shell_stats.successful_commands++;
        }
    } else {
        shell_print_error("Unknown command");
        vga_writestring("Type 'help' for available commands.\n");
        shell_stats.errors++;
    }
    
    shell_free_args(argc, argv);
}

void shell_parse_command(const char *command_line, int *argc, char ***argv) {
    *argc = 0;
    *argv = NULL;
    
    if (!command_line || strlen(command_line) == 0) {
        return;
    }
    
    /* Allocate argv array */
    *argv = (char**)kcalloc(SHELL_MAX_ARGS, sizeof(char*));
    if (!*argv) {
        return;
    }
    
    /* Parse tokens */
    char *line_copy = kstrdup(command_line);
    if (!line_copy) {
        kfree(*argv);
        *argv = NULL;
        return;
    }
    
    char *token = line_copy;
    char *next_token;
    
    while (*argc < SHELL_MAX_ARGS && token && *token) {
        /* Skip leading whitespace */
        while (*token == ' ' || *token == '\t') {
            token++;
        }
        
        if (!*token) break;
        
        /* Find end of token */
        next_token = token;
        while (*next_token && *next_token != ' ' && *next_token != '\t') {
            next_token++;
        }
        
        /* Null-terminate token */
        if (*next_token) {
            *next_token = '\0';
            next_token++;
        }
        
        /* Allocate and copy token */
        (*argv)[*argc] = kstrdup(token);
        if (!(*argv)[*argc]) {
            shell_free_args(*argc, *argv);
            kfree(line_copy);
            *argc = 0;
            *argv = NULL;
            return;
        }
        
        (*argc)++;
        token = next_token;
    }
    
    kfree(line_copy);
}

void shell_free_args(int argc, char **argv) {
    if (!argv) return;
    
    for (int i = 0; i < argc; i++) {
        if (argv[i]) {
            kfree(argv[i]);
        }
    }
    kfree(argv);
}

void shell_print_prompt(void) {
    vga_setcolor(vga_entry_color(SHELL_PROMPT_COLOR, VGA_COLOR_BLACK));
    vga_writestring("NumOS");
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_putchar(':');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
    vga_putchar('>');
    vga_putchar(' ');
    vga_setcolor(vga_entry_color(SHELL_INPUT_COLOR, VGA_COLOR_BLACK));
}

void shell_print_welcome(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS Shell v2.1\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Type 'help' for available commands.\n\n");
}

void shell_print_error(const char *message) {
    vga_setcolor(vga_entry_color(SHELL_ERROR_COLOR, VGA_COLOR_BLACK));
    vga_writestring("Error: ");
    vga_writestring(message);
    vga_putchar('\n');
    vga_setcolor(vga_entry_color(SHELL_INPUT_COLOR, VGA_COLOR_BLACK));
}

void shell_print_success(const char *message) {
    vga_setcolor(vga_entry_color(SHELL_SUCCESS_COLOR, VGA_COLOR_BLACK));
    vga_writestring(message);
    vga_putchar('\n');
    vga_setcolor(vga_entry_color(SHELL_INPUT_COLOR, VGA_COLOR_BLACK));
}

void shell_clear_screen(void) {
    vga_clear();
}

int shell_register_command(const char *name, const char *description, 
                          void (*handler)(int argc, char **argv), 
                          int min_args, int max_args) {
    if (registered_commands >= MAX_COMMANDS) {
        return -1; /* Registry full */
    }
    
    struct shell_command *cmd = &command_registry[registered_commands];
    cmd->name = name;
    cmd->description = description;
    cmd->handler = handler;
    cmd->min_args = min_args;
    cmd->max_args = max_args;
    
    registered_commands++;
    return 0;
}

struct shell_command* shell_find_command(const char *name) {
    for (int i = 0; i < registered_commands; i++) {
        if (strcmp(command_registry[i].name, name) == 0) {
            return &command_registry[i];
        }
    }
    return NULL;
}

struct shell_stats shell_get_stats(void) {
    shell_stats.uptime_ms = timer_get_uptime_ms() - shell_state.start_time;
    return shell_stats;
}

void shell_print_stats(void) {
    struct shell_stats stats = shell_get_stats();
    
    vga_writestring("Shell Statistics:\n");
    vga_writestring("  Commands executed: ");
    print_dec(stats.commands_executed);
    vga_writestring("\n  Successful:        ");
    print_dec(stats.successful_commands);
    vga_writestring("\n  Errors:            ");
    print_dec(stats.errors);
    vga_writestring("\n  Shell uptime:      ");
    print_dec(stats.uptime_ms);
    vga_writestring(" ms\n");
}

/* Helper Functions */
static int shell_parse_hex(const char *hex_str, uint64_t *value) {
    *value = 0;
    
    if (hex_str[0] == '0' && hex_str[1] == 'x') {
        hex_str += 2; /* Skip "0x" */
    }
    
    while (*hex_str) {
        char c = *hex_str++;
        *value <<= 4;
        if (c >= '0' && c <= '9') {
            *value += c - '0';
        } else if (c >= 'a' && c <= 'f') {
            *value += c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            *value += c - 'A' + 10;
        } else {
            return -1; /* Invalid hex character */
        }
    }
    
    return 0;
}

static int shell_parse_dec(const char *dec_str, uint32_t *value) {
    *value = 0;
    
    while (*dec_str) {
        char c = *dec_str++;
        if (c >= '0' && c <= '9') {
            *value = *value * 10 + (c - '0');
        } else {
            return -1; /* Invalid decimal character */
        }
    }
    
    return 0;
}

static void shell_register_builtin_commands(void) {
    /* Basic commands */
    shell_register_command("help", "- Show available commands", shell_cmd_help, 0, 0);
    shell_register_command("clear", "- Clear the screen", shell_cmd_clear, 0, 0);
    shell_register_command("version", "- Show system version", shell_cmd_version, 0, 0);
    shell_register_command("echo", "<text> - Echo back text", shell_cmd_echo, 1, -1);
    shell_register_command("exit", "- Exit the shell", shell_cmd_exit, 0, 0);
    shell_register_command("reboot", "- Restart the system", shell_cmd_reboot, 0, 0);
    
    /* System information */
    shell_register_command("uptime", "- Show system uptime", shell_cmd_uptime, 0, 0);
    shell_register_command("meminfo", "- Show memory information", shell_cmd_meminfo, 0, 0);
    shell_register_command("heapinfo", "- Show heap statistics", shell_cmd_heapinfo, 0, 0);
    shell_register_command("paging", "- Show paging status", shell_cmd_paging, 0, 0);
    shell_register_command("pagingstats", "- Show paging statistics", shell_cmd_pagingstats, 0, 0);
    shell_register_command("vmregions", "- Show virtual memory regions", shell_cmd_vmregions, 0, 0);
    shell_register_command("timer", "- Show timer information", shell_cmd_timer, 0, 0);
    
    /* Test commands */
    shell_register_command("testpage", "- Test page allocation", shell_cmd_testpage, 0, 0);
    shell_register_command("testheap", "- Test heap allocation", shell_cmd_testheap, 0, 0);
    shell_register_command("benchmark", "- Run memory benchmarks", shell_cmd_benchmark, 0, 0);
    
    /* Utility commands */
    shell_register_command("translate", "<addr> - Translate virtual to physical address", shell_cmd_translate, 1, 1);
    shell_register_command("sleep", "<ms> - Sleep for specified milliseconds", shell_cmd_sleep, 1, 1);
    
    /* FAT32 commands */
    shell_register_command("fat32init", "- Initialize FAT32 filesystem", shell_cmd_fat32init, 0, 0);
    shell_register_command("fat32mount", "- Mount FAT32 filesystem", shell_cmd_fat32mount, 0, 0);
    shell_register_command("fat32unmount", "- Unmount FAT32 filesystem", shell_cmd_fat32unmount, 0, 0);
    shell_register_command("ls", "- List files in directory", shell_cmd_ls, 0, 0);
    shell_register_command("dir", "- List files in directory", shell_cmd_ls, 0, 0);
    shell_register_command("cat", "<file> - Display file contents", shell_cmd_cat, 1, 1);
    shell_register_command("create", "<file> - Create a new file", shell_cmd_create, 1, 1);
    shell_register_command("write", "<file> <text> - Write text to file", shell_cmd_write, 2, -1);
    shell_register_command("fileinfo", "<file> - Show file information", shell_cmd_fileinfo, 1, 1);
    shell_register_command("exists", "<file> - Check if file exists", shell_cmd_exists, 1, 1);
    shell_register_command("bootinfo", "- Show FAT32 boot sector info", shell_cmd_bootinfo, 0, 0);
    shell_register_command("fsinfo", "- Show FAT32 filesystem info", shell_cmd_fsinfo, 0, 0);
    shell_register_command("testfat32", "- Run FAT32 filesystem tests", shell_cmd_testfat32, 0, 0);

        shell_register_command("lsdisk", "List available disks", shell_cmd_lsdisk, 0, 0);
    shell_register_command("diskinfo", "Show disk information", shell_cmd_diskinfo, 1, 1);
    shell_register_command("diskcache", "Show disk cache statistics", shell_cmd_diskcache, 1, 1);
    shell_register_command("diskflush", "Flush disk cache", shell_cmd_diskflush, 1, 1);
    shell_register_command("createimage", "Create disk image", shell_cmd_createimage, 2, 2);
    shell_register_command("mountimage", "Mount disk image", shell_cmd_mountimage, 2, 2);
    shell_register_command("unmountdisk", "Unmount disk", shell_cmd_unmountdisk, 1, 1);
    shell_register_command("diskread", "Read disk sector", shell_cmd_diskread, 2, 2);
    shell_register_command("diskwrite", "Write disk sector", shell_cmd_diskwrite, 3, 3);
    shell_register_command("disktest", "Test disk I/O", shell_cmd_disktest, 0, 0);


}

/* Built-in Command Implementations */
void shell_cmd_help(int argc, char **argv) {
    (void)argc; (void)argv; /* Suppress unused warnings */
    
    vga_writestring("Available Commands:\n\n");
    
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("=== Basic Commands ===\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    for (int i = 0; i < registered_commands; i++) {
        struct shell_command *cmd = &command_registry[i];
        vga_writestring("  ");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring(cmd->name);
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        vga_writestring(" ");
        vga_writestring(cmd->description);
        vga_putchar('\n');
    }
    vga_putchar('\n');
}

void shell_cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_clear_screen();
}

void shell_cmd_version(int argc, char **argv) {
    (void)argc; (void)argv;
    
    vga_writestring("NumOS Version 2.1\n");
    vga_writestring("64-bit Operating System with Advanced Features\n");
    vga_writestring("- Enhanced paging with VM regions\n");
    vga_writestring("- Kernel heap allocator (kmalloc/kfree)\n");
    vga_writestring("- Timer driver with PIT support\n");
    vga_writestring("- FAT32 filesystem support\n");
    vga_writestring("- Modular shell system\n");
    vga_writestring("- Built with C and Assembly\n");
}

void shell_cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        vga_writestring(argv[i]);
        if (i < argc - 1) {
            vga_putchar(' ');
        }
    }
    vga_putchar('\n');
}

void shell_cmd_exit(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_print_success("Goodbye!");
    shell_state.running = 0;
}

void shell_cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    
    vga_writestring("Rebooting system...\n");
    /* Simple reboot via keyboard controller */
    outb(0x64, 0xFE);
    hang();
}

void shell_cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    
    uint64_t uptime_ms = timer_get_uptime_ms();
    uint64_t seconds = uptime_ms / 1000;
    uint64_t minutes = seconds / 60;
    uint64_t hours = minutes / 60;
    
    vga_writestring("System uptime: ");
    if (hours > 0) {
        print_dec(hours);
        vga_writestring("h ");
    }
    if (minutes > 0) {
        print_dec(minutes % 60);
        vga_writestring("m ");
    }
    print_dec(seconds % 60);
    vga_writestring("s (");
    print_dec(uptime_ms);
    vga_writestring(" ms)\n");
}

void shell_cmd_meminfo(int argc, char **argv) {
    (void)argc; (void)argv;
    
    vga_writestring("Memory Information:\n");
    vga_writestring("  Total frames: ");
    print_dec(pmm_get_total_frames());
    vga_writestring("\n  Used frames:  ");
    print_dec(pmm_get_used_frames());
    vga_writestring("\n  Free frames:  ");
    print_dec(pmm_get_free_frames());
    vga_writestring("\n  Frame size:   4096 bytes\n");
}

void shell_cmd_heapinfo(int argc, char **argv) {
    (void)argc; (void)argv;
    heap_print_stats();
}

void shell_cmd_paging(int argc, char **argv) {
    (void)argc; (void)argv;
    
    vga_writestring("Paging System Status:\n");
    paging_enable(); /* This will print status */
    vga_writestring("  Page size: 4096 bytes (4 KB)\n");
    vga_writestring("  Large page size: 2097152 bytes (2 MB)\n");
    vga_writestring("  4-level paging active (PML4)\n");
    vga_writestring("  Enhanced features: VM regions, bulk operations\n");
}

void shell_cmd_pagingstats(int argc, char **argv) {
    (void)argc; (void)argv;
    paging_print_stats();
}

void shell_cmd_vmregions(int argc, char **argv) {
    (void)argc; (void)argv;
    paging_print_vm_regions();
}

void shell_cmd_timer(int argc, char **argv) {
    (void)argc; (void)argv;
    
    struct timer_stats stats = timer_get_stats();
    vga_writestring("Timer Information:\n");
    vga_writestring("  Frequency:    ");
    print_dec(stats.frequency);
    vga_writestring(" Hz\n");
    vga_writestring("  Total ticks:  ");
    print_dec(stats.ticks);
    vga_writestring("\n  Uptime:       ");
    print_dec(stats.seconds);
    vga_writestring(" seconds\n");
}

void shell_cmd_testpage(int argc, char **argv) {
    (void)argc; (void)argv;
    
    vga_writestring("Testing page allocation...\n");
    
    /* Allocate some pages */
    void *pages = vmm_alloc_pages(2, PAGE_PRESENT | PAGE_WRITABLE);
    if (pages) {
        vga_writestring("Allocated 2 pages at virtual address: ");
        print_hex((uint64_t)pages);
        
        /* Test writing to the pages */
        char *test_ptr = (char*)pages;
        *test_ptr = 'A';
        *(test_ptr + 4096) = 'B'; /* Second page */
        
        vga_writestring("\nWrote test data successfully\n");
        vga_writestring("First page data: ");
        vga_putchar(*test_ptr);
        vga_writestring("\nSecond page data: ");
        vga_putchar(*(test_ptr + 4096));
        vga_putchar('\n');
        
        /* Test mapping validation */
        if (paging_validate_range((uint64_t)pages, 2)) {
            shell_print_success("Page mapping validation: PASSED");
        } else {
            shell_print_error("Page mapping validation: FAILED");
        }
        
        /* Free the pages */
        vmm_free_pages(pages, 2);
        shell_print_success("Pages freed successfully");
    } else {
        shell_print_error("Failed to allocate pages");
    }
}

void shell_cmd_testheap(int argc, char **argv) {
    (void)argc; (void)argv;
    
    vga_writestring("Testing heap allocation...\n");
    
    /* Test basic allocation */
    void *ptr1 = kmalloc(100);
    void *ptr2 = kzalloc(200);
    char *str = kstrdup("Hello, NumOS Shell!");
    
    if (ptr1 && ptr2 && str) {
        shell_print_success("Basic allocation test: PASSED");
        vga_writestring("Duplicated string: ");
        vga_writestring(str);
        vga_putchar('\n');
        
        /* Test reallocation */
        ptr1 = krealloc(ptr1, 500);
        if (ptr1) {
            shell_print_success("Reallocation test: PASSED");
        } else {
            shell_print_error("Reallocation test: FAILED");
        }
        
        /* Free everything */
        kfree(ptr1);
        kfree(ptr2);
        kfree(str);
        shell_print_success("Memory freed successfully");
    } else {
        shell_print_error("Basic allocation test: FAILED");
    }
    
    /* Validate heap */
    if (heap_validate()) {
        shell_print_success("Heap validation: PASSED");
    } else {
        shell_print_error("Heap validation: FAILED");
    }
}

void shell_cmd_benchmark(int argc, char **argv) {
    (void)argc; (void)argv;
    
    vga_writestring("Running memory allocation benchmark...\n");
    
    uint64_t start_bench = timer_benchmark_start();
    
    /* Allocate and free many small blocks */
    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        ptrs[i] = kmalloc(64);
        if (!ptrs[i]) {
            vga_writestring("Allocation failed at iteration ");
            print_dec(i);
            vga_putchar('\n');
            break;
        }
    }
    
    /* Free all blocks */
    for (int i = 0; i < 100; i++) {
        if (ptrs[i]) {
            kfree(ptrs[i]);
        }
    }
    
    uint64_t elapsed_ms = timer_benchmark_end(start_bench);
    
    vga_writestring("Benchmark completed in ");
    print_dec(elapsed_ms);
    vga_writestring(" ms\n");
    
    /* Test large allocation */
    start_bench = timer_benchmark_start();
    void *large_ptr = kmalloc(1024 * 1024); /* 1MB */
    if (large_ptr) {
        /* Write pattern to test */
        char *test_ptr = (char*)large_ptr;
        for (int i = 0; i < 1000; i++) {
            test_ptr[i] = (char)(i % 256);
        }
        
        /* Verify pattern */
        int errors = 0;
        for (int i = 0; i < 1000; i++) {
            if (test_ptr[i] != (char)(i % 256)) {
                errors++;
            }
        }
        
        kfree(large_ptr);
        elapsed_ms = timer_benchmark_end(start_bench);
        
        vga_writestring("Large allocation test (1MB): ");
        if (errors == 0) {
            vga_writestring("PASSED");
        } else {
            vga_writestring("FAILED (");
            print_dec(errors);
            vga_writestring(" errors)");
        }
        vga_writestring(" in ");
        print_dec(elapsed_ms);
        vga_writestring(" ms\n");
    } else {
        shell_print_error("Large allocation test: FAILED (out of memory)");
    }
}

void shell_cmd_translate(int argc, char **argv) {
    uint64_t virtual_addr;
    
    if (shell_parse_hex(argv[1], &virtual_addr) != 0) {
        shell_print_error("Invalid hex address");
        return;
    }
    
    uint64_t physical_addr = paging_get_physical_address(virtual_addr);
    
    vga_writestring("Virtual address:  ");
    print_hex(virtual_addr);
    vga_writestring("\nPhysical address: ");
    if (physical_addr) {
        print_hex(physical_addr);
    } else {
        vga_writestring("Not mapped");
    }
    vga_putchar('\n');
}

void shell_cmd_sleep(int argc, char **argv) {
    uint32_t ms;
    
    if (shell_parse_dec(argv[1], &ms) != 0) {
        shell_print_error("Invalid sleep duration");
        return;
    }
    
    if (ms > 10000) { /* Max 10 seconds */
        shell_print_error("Sleep duration too long (max 10000 ms)");
        return;
    }
    
    vga_writestring("Sleeping for ");
    print_dec(ms);
    vga_writestring(" ms...\n");
    
    uint64_t start_time = timer_get_uptime_ms();
    timer_sleep(ms);
    uint64_t end_time = timer_get_uptime_ms();
    
    vga_writestring("Woke up after ");
    print_dec(end_time - start_time);
    vga_writestring(" ms\n");
}

/* FAT32 Command Implementations */
void shell_cmd_fat32init(int argc, char **argv) {
    (void)argc; (void)argv;
    
    vga_writestring("Initializing FAT32 filesystem...\n");
    int result = fat32_init();
    if (result == FAT32_SUCCESS) {
        shell_print_success("FAT32 initialization successful");
    } else {
        vga_writestring("FAT32 initialization failed with code ");
        print_dec(result);
        vga_putchar('\n');
    }
}

void shell_cmd_fat32mount(int argc, char **argv) {
    (void)argc; (void)argv;
    
    vga_writestring("Mounting FAT32 filesystem...\n");
    int result = fat32_mount();
    if (result == FAT32_SUCCESS) {
        shell_print_success("FAT32 filesystem mounted successfully");
    } else {
        vga_writestring("FAT32 mount failed with code ");
        print_dec(result);
        vga_putchar('\n');
    }
}

void shell_cmd_fat32unmount(int argc, char **argv) {
    (void)argc; (void)argv;
    
    fat32_unmount();
    shell_print_success("FAT32 filesystem unmounted");
}

void shell_cmd_ls(int argc, char **argv) {
    (void)argc; (void)argv;
    fat32_list_files();
}

void shell_cmd_cat(int argc, char **argv) {
    const char *filename = argv[1];
    
    struct fat32_file *file = fat32_fopen(filename, "r");
    if (!file) {
        shell_print_error("Failed to open file");
        return;
    }
    
    char buffer[256];
    size_t bytes_read;
    vga_writestring("File contents:\n");
    vga_writestring("--- ");
    vga_writestring(filename);
    vga_writestring(" ---\n");
    
    while ((bytes_read = fat32_fread(buffer, 1, sizeof(buffer) - 1, file)) > 0) {
        buffer[bytes_read] = '\0';
        vga_writestring(buffer);
    }
    
    vga_writestring("\n--- End of file ---\n");
    fat32_fclose(file);
}

void shell_cmd_create(int argc, char **argv) {
    const char *filename = argv[1];
    
    struct fat32_file *file = fat32_fopen(filename, "w");
    if (!file) {
        shell_print_error("Failed to create file");
        return;
    }
    
    fat32_fclose(file);
    vga_writestring("File created: ");
    vga_writestring(filename);
    vga_putchar('\n');
}

void shell_cmd_write(int argc, char **argv) {
    const char *filename = argv[1];
    
    /* Concatenate all remaining arguments as the text to write */
    size_t total_len = 0;
    for (int i = 2; i < argc; i++) {
        total_len += strlen(argv[i]) + 1; /* +1 for space or null terminator */
    }
    
    char *text = (char*)kmalloc(total_len);
    if (!text) {
        shell_print_error("Out of memory");
        return;
    }
    
    text[0] = '\0';
    for (int i = 2; i < argc; i++) {
        strcat(text, argv[i]);
        if (i < argc - 1) {
            strcat(text, " ");
        }
    }
    
    struct fat32_file *file = fat32_fopen(filename, "w");
    if (!file) {
        shell_print_error("Failed to open file for writing");
        kfree(text);
        return;
    }
    
    size_t text_len = strlen(text);
    size_t written = fat32_fwrite(text, 1, text_len, file);
    fat32_fclose(file);
    kfree(text);
    
    vga_writestring("Wrote ");
    print_dec(written);
    vga_writestring(" bytes to ");
    vga_writestring(filename);
    vga_putchar('\n');
}

void shell_cmd_fileinfo(int argc, char **argv) {
    const char *filename = argv[1];
    fat32_print_file_info(filename);
}

void shell_cmd_exists(int argc, char **argv) {
    const char *filename = argv[1];
    
    if (fat32_exists(filename)) {
        vga_writestring("File exists: ");
        vga_writestring(filename);
        vga_writestring(" (");
        print_dec(fat32_get_file_size(filename));
        vga_writestring(" bytes)\n");
    } else {
        vga_writestring("File does not exist: ");
        vga_writestring(filename);
        vga_putchar('\n');
    }
}

void shell_cmd_bootinfo(int argc, char **argv) {
    (void)argc; (void)argv;
    fat32_print_boot_sector();
}

void shell_cmd_fsinfo(int argc, char **argv) {
    (void)argc; (void)argv;
    fat32_print_fs_info();
}

void shell_cmd_testfat32(int argc, char **argv) {
    (void)argc; (void)argv;
    
    vga_writestring("Running FAT32 filesystem tests...\n");
    
    /* Test 1: Initialize and mount */
    vga_writestring("Test 1: Initialize and mount filesystem...\n");
    if (fat32_init() != FAT32_SUCCESS) {
        shell_print_error("FAILED: Could not initialize FAT32");
        return;
    }
    if (fat32_mount() != FAT32_SUCCESS) {
        shell_print_error("FAILED: Could not mount FAT32");
        return;
    }
    shell_print_success("PASSED: Filesystem initialized and mounted");
    
    /* Test 2: Create and write to a file */
    vga_writestring("Test 2: Create and write to file...\n");
    struct fat32_file *file = fat32_fopen("test.txt", "w");
    if (!file) {
        shell_print_error("FAILED: Could not create test file");
        return;
    }
    
    const char *test_data = "Hello, FAT32 World!\nThis is a test file.";
    size_t written = fat32_fwrite(test_data, 1, strlen(test_data), file);
    fat32_fclose(file);
    
    if (written == strlen(test_data)) {
        vga_writestring("  PASSED: File written successfully (");
        print_dec(written);
        vga_writestring(" bytes)\n");
    } else {
        shell_print_error("FAILED: File write incomplete");
        return;
    }
    
    /* Test 3: Read back the file */
    vga_writestring("Test 3: Read back file contents...\n");
    file = fat32_fopen("test.txt", "r");
    if (!file) {
        shell_print_error("FAILED: Could not open test file for reading");
        return;
    }
    
    char read_buffer[256];
    size_t read_bytes = fat32_fread(read_buffer, 1, sizeof(read_buffer) - 1, file);
    read_buffer[read_bytes] = '\0';
    fat32_fclose(file);
    
    if (read_bytes == strlen(test_data) && strcmp(read_buffer, test_data) == 0) {
        shell_print_success("PASSED: File read successfully");
        vga_writestring("  Content: ");
        vga_writestring(read_buffer);
        vga_putchar('\n');
    } else {
        shell_print_error("FAILED: File read mismatch");
        vga_writestring("  Expected: ");
        vga_writestring(test_data);
        vga_writestring("\n  Got:      ");
        vga_writestring(read_buffer);
        vga_putchar('\n');
        return;
    }
    
    /* Test 4: File existence check */
    vga_writestring("Test 4: File existence check...\n");
    if (fat32_exists("test.txt") && fat32_get_file_size("test.txt") == strlen(test_data)) {
        shell_print_success("PASSED: File exists with correct size");
    } else {
        shell_print_error("FAILED: File existence/size check failed");
        return;
    }
    
    shell_print_success("All FAT32 tests PASSED!");
}

/* Disk Management Commands */
void shell_cmd_lsdisk(int argc, char **argv) {
    (void)argc; (void)argv;
    disk_list_disks();
}

void shell_cmd_diskinfo(int argc, char **argv) {
    if (argc < 2) {
        shell_print_error("Usage: diskinfo <disk_id>");
        return;
    }
    
    uint8_t disk_id = (uint8_t)strtol(argv[1], NULL, 10);
    disk_print_info(disk_id);
}

void shell_cmd_diskcache(int argc, char **argv) {
    if (argc < 2) {
        shell_print_error("Usage: diskcache <disk_id>");
        return;
    }
    
    uint8_t disk_id = (uint8_t)strtol(argv[1], NULL, 10);
    disk_print_cache_stats(disk_id);
}

void shell_cmd_diskflush(int argc, char **argv) {
    if (argc < 2) {
        shell_print_error("Usage: diskflush <disk_id>");
        return;
    }
    
    uint8_t disk_id = (uint8_t)strtol(argv[1], NULL, 10);
    struct disk_handle *disk = disk_open(disk_id);
    if (!disk) {
        shell_print_error("Failed to open disk");
        return;
    }
    
    int result = disk_flush_cache(disk);
    if (result == DISK_SUCCESS) {
        shell_print_success("Disk cache flushed successfully");
    } else {
        shell_print_error("Failed to flush disk cache");
    }
    
    disk_close(disk);
}

void shell_cmd_createimage(int argc, char **argv) {
    if (argc < 3) {
        shell_print_error("Usage: createimage <filename> <size_mb>");
        return;
    }
    
    const char *filename = argv[1];
    uint64_t size_mb = (uint64_t)strtol(argv[2], NULL, 10);
    uint64_t size_bytes = size_mb * 1024 * 1024;
    
    vga_writestring("Creating disk image '");
    vga_writestring(filename);
    vga_writestring("' (");
    print_dec(size_mb);
    vga_writestring("MB)...\n");
    
    int result = disk_create_image(filename, size_bytes);
    if (result == DISK_SUCCESS) {
        shell_print_success("Disk image created successfully");
    } else {
        shell_print_error("Failed to create disk image");
    }
}

void shell_cmd_mountimage(int argc, char **argv) {
    if (argc < 3) {
        shell_print_error("Usage: mountimage <filename> <disk_id>");
        return;
    }
    
    const char *filename = argv[1];
    uint8_t disk_id = (uint8_t)strtol(argv[2], NULL, 10);
    
    vga_writestring("Mounting disk image '");
    vga_writestring(filename);
    vga_writestring("' as disk ");
    print_dec(disk_id);
    vga_writestring("...\n");
    
    int result = disk_mount_image(filename, disk_id);
    if (result == DISK_SUCCESS) {
        shell_print_success("Disk image mounted successfully");
    } else {
        shell_print_error("Failed to mount disk image");
    }
}

void shell_cmd_unmountdisk(int argc, char **argv) {
    if (argc < 2) {
        shell_print_error("Usage: unmountdisk <disk_id>");
        return;
    }
    
    uint8_t disk_id = (uint8_t)strtol(argv[1], NULL, 10);
    
    int result = disk_unmount(disk_id);
    if (result == DISK_SUCCESS) {
        shell_print_success("Disk unmounted successfully");
    } else {
        shell_print_error("Failed to unmount disk");
    }
}

void shell_cmd_diskread(int argc, char **argv) {
    if (argc < 3) {
        shell_print_error("Usage: diskread <disk_id> <sector>");
        return;
    }
    
    uint8_t disk_id = (uint8_t)strtol(argv[1], NULL, 10);
    uint32_t sector = (uint32_t)strtol(argv[2], NULL, 10);
    
    struct disk_handle *disk = disk_open(disk_id);
    if (!disk) {
        shell_print_error("Failed to open disk");
        return;
    }
    
    uint8_t *buffer = kmalloc(512);
    if (!buffer) {
        shell_print_error("Failed to allocate buffer");
        disk_close(disk);
        return;
    }
    
    int result = disk_read_sector(disk, sector, buffer);
    if (result == DISK_SUCCESS) {
        vga_writestring("Sector ");
        print_dec(sector);
        vga_writestring(" contents:\n");
        print_memory(buffer, 512);
    } else {
        shell_print_error("Failed to read sector");
    }
    
    kfree(buffer);
    disk_close(disk);
}

void shell_cmd_diskwrite(int argc, char **argv) {
    if (argc < 4) {
        shell_print_error("Usage: diskwrite <disk_id> <sector> <data>");
        shell_print_error("Example: diskwrite 0 1 'Hello World'");
        return;
    }
    
    uint8_t disk_id = (uint8_t)strtol(argv[1], NULL, 10);
    uint32_t sector = (uint32_t)strtol(argv[2], NULL, 10);
    const char *data = argv[3];
    
    struct disk_handle *disk = disk_open(disk_id);
    if (!disk) {
        shell_print_error("Failed to open disk");
        return;
    }
    
    uint8_t *buffer = kzalloc(512);
    if (!buffer) {
        shell_print_error("Failed to allocate buffer");
        disk_close(disk);
        return;
    }
    
    /* Copy data to buffer (max 512 bytes) */
    size_t data_len = strlen(data);
    if (data_len > 511) data_len = 511;
    memcpy(buffer, data, data_len);
    
    int result = disk_write_sector(disk, sector, buffer);
    if (result == DISK_SUCCESS) {
        shell_print_success("Data written to sector successfully");
        
        /* Flush to ensure persistence */
        disk_flush_cache(disk);
    } else {
        shell_print_error("Failed to write sector");
    }
    
    kfree(buffer);
    disk_close(disk);
}

void shell_cmd_disktest(int argc, char **argv) {
    (void)argc; (void)argv;
    
    vga_writestring("Disk subsystem test:\n");
    
    /* Test disk 0 */
    struct disk_handle *disk = disk_open(0);
    if (!disk) {
        shell_print_error("Failed to open disk 0");
        return;
    }
    
    vga_writestring("Testing disk 0...\n");
    
    /* Test write */
    uint8_t test_data[512];
    memset(test_data, 0, 512);
    strcpy((char*)test_data, "NumOS Disk Test - This data should persist!");
    
    vga_writestring("Writing test data to sector 100...\n");
    int result = disk_write_sector(disk, 100, test_data);
    if (result != DISK_SUCCESS) {
        shell_print_error("Write test failed");
        disk_close(disk);
        return;
    }
    
    /* Test read */
    uint8_t read_buffer[512];
    memset(read_buffer, 0, 512);
    
    vga_writestring("Reading test data from sector 100...\n");
    result = disk_read_sector(disk, 100, read_buffer);
    if (result != DISK_SUCCESS) {
        shell_print_error("Read test failed");
        disk_close(disk);
        return;
    }
    
    /* Verify data */
    if (memcmp(test_data, read_buffer, 512) == 0) {
        shell_print_success("Disk test passed - data matches!");
    } else {
        shell_print_error("Disk test failed - data mismatch!");
        vga_writestring("Expected: ");
        vga_writestring((char*)test_data);
        vga_writestring("\nActual: ");
        vga_writestring((char*)read_buffer);
        vga_putchar('\n');
    }
    
    /* Flush cache */
    disk_flush_cache(disk);
    disk_close(disk);
    
    vga_writestring("Test completed.\n");
}