#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>
#include <stddef.h>

/* Shell Configuration */
#define SHELL_BUFFER_SIZE       256
#define SHELL_MAX_ARGS          16
#define SHELL_PROMPT_COLOR      VGA_COLOR_LIGHT_GREEN
#define SHELL_INPUT_COLOR       VGA_COLOR_LIGHT_GREY
#define SHELL_ERROR_COLOR       VGA_COLOR_LIGHT_RED
#define SHELL_SUCCESS_COLOR     VGA_COLOR_LIGHT_GREEN

/* Shell Command Structure */
struct shell_command {
    const char *name;           /* Command name */
    const char *description;    /* Command description */
    void (*handler)(int argc, char **argv);  /* Command handler function */
    int min_args;               /* Minimum number of arguments */
    int max_args;               /* Maximum number of arguments (-1 = unlimited) */
};

/* Shell State */
struct shell_state {
    char *buffer;               /* Command input buffer */
    size_t buffer_size;         /* Buffer size */
    int running;                /* Shell running flag */
    uint32_t command_count;     /* Total commands executed */
    uint64_t start_time;        /* Shell start time */
};

/* Core Shell Functions */
void shell_init(void);
void shell_run(void);
void shell_shutdown(void);

/* Command Processing */
void shell_process_command(const char *command_line);
void shell_parse_command(const char *command_line, int *argc, char ***argv);
void shell_free_args(int argc, char **argv);

/* Shell Utilities */
void shell_print_prompt(void);
void shell_print_welcome(void);
void shell_print_help(void);
void shell_print_error(const char *message);
void shell_print_success(const char *message);
void shell_clear_screen(void);

/* Command Registration */
int shell_register_command(const char *name, const char *description, 
                          void (*handler)(int argc, char **argv), 
                          int min_args, int max_args);
struct shell_command* shell_find_command(const char *name);

/* Built-in Command Handlers */
void shell_cmd_help(int argc, char **argv);
void shell_cmd_clear(int argc, char **argv);
void shell_cmd_version(int argc, char **argv);
void shell_cmd_echo(int argc, char **argv);
void shell_cmd_exit(int argc, char **argv);
void shell_cmd_reboot(int argc, char **argv);

/* System Information Commands */
void shell_cmd_uptime(int argc, char **argv);
void shell_cmd_meminfo(int argc, char **argv);
void shell_cmd_heapinfo(int argc, char **argv);
void shell_cmd_paging(int argc, char **argv);
void shell_cmd_pagingstats(int argc, char **argv);
void shell_cmd_vmregions(int argc, char **argv);
void shell_cmd_timer(int argc, char **argv);

/* Test Commands */
void shell_cmd_testpage(int argc, char **argv);
void shell_cmd_testheap(int argc, char **argv);
void shell_cmd_benchmark(int argc, char **argv);

/* Utility Commands */
void shell_cmd_translate(int argc, char **argv);
void shell_cmd_sleep(int argc, char **argv);

/* FAT32 Commands */
void shell_cmd_fat32init(int argc, char **argv);
void shell_cmd_fat32mount(int argc, char **argv);
void shell_cmd_fat32unmount(int argc, char **argv);
void shell_cmd_ls(int argc, char **argv);
void shell_cmd_cat(int argc, char **argv);
void shell_cmd_create(int argc, char **argv);
void shell_cmd_write(int argc, char **argv);
void shell_cmd_fileinfo(int argc, char **argv);
void shell_cmd_exists(int argc, char **argv);
void shell_cmd_bootinfo(int argc, char **argv);
void shell_cmd_fsinfo(int argc, char **argv);
void shell_cmd_testfat32(int argc, char **argv);

/* Shell Statistics */
struct shell_stats {
    uint32_t commands_executed;
    uint64_t uptime_ms;
    uint32_t errors;
    uint32_t successful_commands;
};

struct shell_stats shell_get_stats(void);
void shell_print_stats(void);

#endif /* SHELL_H */