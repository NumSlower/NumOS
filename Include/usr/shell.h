#ifndef SHELL_H
#define SHELL_H

/* Shell initialization and main loop */
void shell_init(void);
void shell_main(void);  /* New main entry point */

/* Compatibility functions */
void shell_run(void);
void shell_shutdown(void);

/* Command registration */
void shell_register_command(const char *name, const char *description, 
                           void (*handler)(int, char**));

#endif