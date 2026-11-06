/* NumOS — tty.h — Terminal subsystem (Phase 12, spec section 11)
 *
 * A terminal is independent of both the shell (a userspace program that
 * attaches to it) and the graphical window manager (graphical terminals
 * will attach through the same API). Provides: input/output streams,
 * keyboard event translation, canonical line discipline with backspace
 * editing, raw mode, echo control, scrollback, dimensions/cursor state,
 * ANSI escape sequences, per-terminal input queues, and foreground-process
 * tracking with Ctrl-C termination as the job-control equivalent.
 */
#ifndef NUMOS_TTY_TTY_H
#define NUMOS_TTY_TTY_H

#include "libs/types.h"
#include "drivers/vga.h"

#define TTY_MAX_TERMINALS 2
#define TTY_LINE_MAX 128
#define TTY_INPUT_LINES 16
#define TTY_SCROLLBACK_LINES 256

#define TTY_MODE_CANONICAL 0
#define TTY_MODE_RAW 1

struct tty {
    uint8_t index;
    uint8_t mode;              /* TTY_MODE_CANONICAL or TTY_MODE_RAW */
    bool echo;
    uint16_t cols;
    uint16_t rows;

    /* Canonical line assembly. */
    char line_buf[TTY_LINE_MAX];
    uint32_t line_len;

    /* Committed input queue (canonical) or direct char queue (raw). */
    char input[TTY_INPUT_LINES][TTY_LINE_MAX];
    uint32_t input_len[TTY_INPUT_LINES];
    uint8_t input_head;
    uint8_t input_tail;
    uint8_t input_count;

    /* Raw-mode single-char overflow queue flags. */
    bool raw_overflow;

    /* Cursor/color state for ANSI restore. */
    uint8_t saved_col;
    uint8_t saved_row;
    uint8_t fg_color;
    uint8_t bg_color;

    /* Screen state for session switching (Phase 12.1). */
    uint16_t screen_save[VGA_WIDTH * VGA_HEIGHT];
    bool screen_valid;
    bool has_shell;            /* session has a shell process attached */

    uint32_t fg_pid;           /* foreground process, 0 = none */
    uint64_t scroll_count;
    char scrollback[TTY_SCROLLBACK_LINES][VGA_WIDTH];
    uint8_t scrollback_attr[TTY_SCROLLBACK_LINES];
    uint32_t scroll_head;
    uint32_t scroll_total;
    uint32_t sb_col;           /* current capture column for the next line */
    char sb_line[VGA_WIDTH];
    uint8_t sb_line_attr;
};

bool tty_init(void);
struct tty* tty_get(uint32_t index);
struct tty* tty_get_active(void);
void tty_poll_keyboard(void);
void tty_inject(struct tty* tty, const char* data, uint32_t len);
int tty_read(struct tty* tty, char* buffer, uint32_t max_len, uint64_t timeout_ticks);
int tty_write(struct tty* tty, const char* buffer, uint32_t len);
void tty_set_mode(struct tty* tty, uint8_t mode);
void tty_set_echo(struct tty* tty, bool echo);
void tty_set_fg_pid(struct tty* tty, uint32_t pid);
uint32_t tty_get_fg_pid(struct tty* tty);
void tty_get_size(struct tty* tty, uint16_t* cols, uint16_t* rows);
void tty_switch_session(uint32_t index);
uint32_t tty_session_count(void);
void tty_attach_shell(struct tty* tty);

#endif
