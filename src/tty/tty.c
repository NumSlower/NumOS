/* NumOS — tty.c — Terminal subsystem implementation
 *
 * Console TTY over VGA text mode with serial mirroring. Output goes
 * through an ANSI ESC [ parser (CUU/CUD/CUF/CUB/CUP/J/K/m/s/u) onto the
 * VGA backend; every scrolled-off line is captured into a 256-line
 * scrollback ring. Input is consumed exclusively from the keyboard
 * driver's queue by tty_poll_keyboard() (process context only). In
 * canonical mode chars assemble in a line buffer with backspace editing
 * and echo; Enter commits the line to the input queue. In raw mode chars
 * go straight to the queue unedited. Ctrl-C requests termination of the
 * foreground process (the Phase 12 job-control equivalent; the scheduler
 * forces the exit at its next kernel boundary).
 */
#include "tty/tty.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/serial.h"
#include "drivers/pit.h"
#include "kernel/spinlock.h"
#include "process/process.h"
#include "process/scheduler.h"
#include "process/userprog.h"
#include "libs/string.h"
#include "libs/kprintf.h"

#define ANSI_MAX_PARAMS 4

static struct tty consoles[TTY_MAX_TERMINALS];
static uint32_t active_terminal = 0;
static spinlock_t tty_lock = {0};

static void tty_render_char(struct tty* t, char c);
static void tty_render_str(struct tty* t, const char* s);

bool tty_init(void) {
    memset(consoles, 0, sizeof(consoles));
    for (uint32_t i = 0; i < TTY_MAX_TERMINALS; i++) {
        consoles[i].index = (uint8_t)i;
        consoles[i].mode = TTY_MODE_CANONICAL;
        consoles[i].echo = true;
        consoles[i].cols = VGA_WIDTH;
        consoles[i].rows = VGA_HEIGHT;
        consoles[i].fg_color = 7;
        consoles[i].bg_color = 0;
        consoles[i].screen_valid = false;
        consoles[i].has_shell = false;
    }
    active_terminal = 0;
    consoles[0].has_shell = true;
    return true;
}

/* Session switching (spec 12.1): save the outgoing session's screen text,
 * restore the incoming one's, and swap cursor/colors. Input queues and
 * per-session state are independent per struct tty, so nothing else is
 * shared. Ctrl-N arrives as control char 0x01+N-1. */
void tty_switch_session(uint32_t index) {
    if (index >= TTY_MAX_TERMINALS || index == active_terminal) return;

    struct tty* from = &consoles[active_terminal];
    struct tty* to = &consoles[index];

    vga_save_screen(from->screen_save);
    from->screen_valid = true;

    vga_get_cursor(&from->saved_col, &from->saved_row);
    vga_get_color(&from->fg_color, &from->bg_color);

    active_terminal = index;

    if (to->screen_valid) {
        vga_restore_screen(to->screen_save);
        vga_set_color(to->fg_color, to->bg_color);
        vga_set_cursor(to->saved_col, to->saved_row);
    } else {
        vga_clear();
        tty_write(to, "\033[1;32mNumOS session ", 20);
        char n = (char)('0' + index);
        tty_write(to, &n, 1);
        tty_write(to, "\033[0m\n", 5);
    }

    if (!to->has_shell) {
        struct process* sh = user_shell_spawn((uint8_t)index);
        to->has_shell = (sh != NULL);
    }
}

uint32_t tty_session_count(void) {
    return TTY_MAX_TERMINALS;
}

void tty_attach_shell(struct tty* tty) {
    if (tty) tty->has_shell = true;
}

struct tty* tty_get(uint32_t index) {
    if (index >= TTY_MAX_TERMINALS) return NULL;
    return &consoles[index];
}

struct tty* tty_get_active(void) {
    return &consoles[active_terminal];
}

void tty_get_size(struct tty* tty, uint16_t* cols, uint16_t* rows) {
    if (!tty || !cols || !rows) return;
    *cols = tty->cols;
    *rows = tty->rows;
}

void tty_set_mode(struct tty* tty, uint8_t mode) {
    if (tty && (mode == TTY_MODE_CANONICAL || mode == TTY_MODE_RAW)) tty->mode = mode;
}

void tty_set_echo(struct tty* tty, bool echo) {
    if (tty) tty->echo = echo;
}

void tty_set_fg_pid(struct tty* tty, uint32_t pid) {
    if (tty) tty->fg_pid = pid;
}

uint32_t tty_get_fg_pid(struct tty* tty) {
    return tty ? tty->fg_pid : 0;
}

/* ---- output rendering --------------------------------------------------- */

static void scrollback_commit(struct tty* t) {
    char* line = t->scrollback[t->scroll_head];
    memset(line, 0, VGA_WIDTH);
    uint32_t n = t->sb_col < VGA_WIDTH ? t->sb_col : VGA_WIDTH;
    memcpy(line, t->sb_line, n);
    t->scrollback_attr[t->scroll_head] = t->sb_line_attr;
    t->scroll_head = (t->scroll_head + 1) % TTY_SCROLLBACK_LINES;
    t->scroll_total++;
    t->sb_col = 0;
}

static void scrollback_char(struct tty* t, char c) {
    if (t->sb_col < VGA_WIDTH) {
        t->sb_line[t->sb_col++] = c;
    }
}

static void ansi_apply_sgr(struct tty* t, int32_t* params, uint32_t count) {
    if (count == 0) {
        t->fg_color = 7;
        t->bg_color = 0;
        vga_set_color(7, 0);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        int32_t p = params[i];
        if (p == 0) {
            t->fg_color = 7;
            t->bg_color = 0;
        } else if (p == 1) {
            if (t->fg_color < 8) t->fg_color += 8;
        } else if (p >= 30 && p <= 37) {
            t->fg_color = (uint8_t)(p - 30);
        } else if (p >= 40 && p <= 47) {
            t->bg_color = (uint8_t)(p - 40);
        }
    }
    t->sb_line_attr = (uint8_t)((t->bg_color << 4) | t->fg_color);
    vga_set_color(t->fg_color, t->bg_color);
}

/* ANSI CSI dispatch. params[] holds the numeric params (missing = -1). */
static void ansi_dispatch(struct tty* t, char final, int32_t* params, uint32_t count) {
    int32_t n1 = (count >= 1 && params[0] >= 0) ? params[0] : 1;
    int32_t n2 = (count >= 2 && params[1] >= 0) ? params[1] : 1;

    switch (final) {
        case 'H': /* cursor absolute (1-based row;col) */
        case 'f':
        {
            uint16_t row = (uint16_t)(n1 > 0 ? n1 - 1 : 0);
            uint16_t col = (uint16_t)(n2 > 0 ? n2 - 1 : 0);
            if (row >= t->rows) row = (uint16_t)(t->rows - 1);
            if (col >= t->cols) col = (uint16_t)(t->cols - 1);
            vga_set_cursor(col, row);
            t->sb_col = col;
            break;
        }
        case 'J': /* erase display */
            if (n1 == 2) {
                vga_clear();
                vga_set_cursor(0, 0);
                t->sb_col = 0;
            }
            break;
        case 'm':
            ansi_apply_sgr(t, params, count);
            break;
        case 's':
            t->saved_col = (uint8_t)t->sb_col;
            t->saved_row = 0;
            break;
        case 'u':
            vga_set_cursor(t->saved_col, t->saved_row);
            t->sb_col = t->saved_col;
            break;
        default:
            break;
    }
}

/* Render one char of output, parsing ANSI escape sequences. */
static void tty_render_char(struct tty* t, char c) {
    static char esc_state = 0;       /* 0=normal, 1=ESC seen, 2=CSI params */
    static int32_t params[ANSI_MAX_PARAMS];
    static uint32_t param_count = 0;
    static int32_t param_cur = 0;
    static bool param_any = false;

    switch (esc_state) {
        case 0:
            if (c == 0x1B) {
                esc_state = 1;
            } else if (c == '\n') {
                scrollback_commit(t);
                vga_putchar('\n');
            } else if (c == '\r') {
                t->sb_col = 0;
            } else if (c == '\b') {
                if (t->sb_col > 0) t->sb_col--;
                vga_putchar('\b');
            } else if (c == '\t') {
                scrollback_char(t, ' ');
                vga_putchar(' ');
            } else if (c >= 32) {
                scrollback_char(t, c);
                vga_putchar(c);
            }
            break;

        case 1: /* after ESC */
            if (c == '[') {
                esc_state = 2;
                param_count = 0;
                param_cur = 0;
                param_any = false;
                for (uint32_t i = 0; i < ANSI_MAX_PARAMS; i++) params[i] = -1;
            } else {
                esc_state = 0; /* unknown escape: drop */
            }
            break;

        case 2: /* collecting params until final byte */
            if (c >= '0' && c <= '9') {
                param_cur = param_cur * 10 + (c - '0');
                param_any = true;
            } else if (c == ';') {
                if (param_count < ANSI_MAX_PARAMS) {
                    params[param_count++] = param_any ? param_cur : -1;
                }
                param_cur = 0;
                param_any = false;
            } else {
                if (param_count < ANSI_MAX_PARAMS) {
                    params[param_count++] = param_any ? param_cur : -1;
                }
                ansi_dispatch(t, c, params, param_count);
                esc_state = 0;
            }
            break;
    }
}

static void tty_render_str(struct tty* t, const char* s) {
    while (*s) {
        tty_render_char(t, *s);
        s++;
    }
}

int tty_write(struct tty* tty, const char* buffer, uint32_t len) {
    if (!tty || !buffer) return -1;
    if (tty != tty_get_active()) return (int)len; /* background terminal: buffered later */

    spin_lock(&tty_lock);
    for (uint32_t i = 0; i < len; i++) {
        tty_render_char(tty, buffer[i]);
    }
    spin_unlock(&tty_lock);

    /* Serial mirror so headless/QEMU-serial runs see console output. */
    serial_write(buffer, len);
    return (int)len;
}

/* ---- input -------------------------------------------------------------- */

static void input_push_line(struct tty* t) {
    if (t->input_count >= TTY_INPUT_LINES) {
        t->input_head = (uint8_t)((t->input_head + 1) % TTY_INPUT_LINES); /* drop oldest */
        t->input_count--;
    }
    uint32_t slot = (t->input_head + t->input_count) % TTY_INPUT_LINES;
    memcpy(t->input[slot], t->line_buf, t->line_len);
    t->input_len[slot] = t->line_len;
    t->input_count++;
}

static bool input_pop_line(struct tty* t, char* buffer, uint32_t max_len, uint32_t* out_len) {
    if (t->input_count == 0) return false;
    uint32_t slot = t->input_head;
    uint32_t n = t->input_len[slot];
    if (n > max_len) n = max_len;
    memcpy(buffer, t->input[slot], n);
    *out_len = n;
    t->input_head = (uint8_t)((t->input_head + 1) % TTY_INPUT_LINES);
    t->input_count--;
    return true;
}

static void tty_handle_char(struct tty* t, char c) {
    if (c >= 0x01 && c <= 0x04) { /* Ctrl-1..Ctrl-4: session switch */
        tty_switch_session((uint32_t)(c - 0x01));
        return;
    }

    if (c == 3) { /* Ctrl-C: job-control equivalent — terminate foreground */
        tty_render_str(t, "^C\n");
        t->line_len = 0;
        if (t->fg_pid) {
            for (uint32_t i = 0; i < PROC_MAX_PROCS; i++) {
                struct process* p = process_get_by_index(i);
                if (p && p->pid == t->fg_pid && p->is_user) {
                    p->terminate_requested = true;
                    break;
                }
            }
        }
        return;
    }

    if (t->mode == TTY_MODE_RAW) {
        if (t->input_count < TTY_INPUT_LINES) {
            uint32_t slot = (t->input_head + t->input_count) % TTY_INPUT_LINES;
            t->input[slot][0] = c;
            t->input_len[slot] = 1;
            t->input_count++;
        }
        if (t->echo) tty_render_char(t, c);
        return;
    }

    /* Canonical line discipline. */
    if (c == '\n' || c == '\r') {
        if (t->echo) tty_render_char(t, '\n');
        input_push_line(t);
        t->line_len = 0;
        return;
    }
    if (c == '\b' || c == 0x7F) {
        if (t->line_len > 0) {
            t->line_len--;
            if (t->echo) tty_render_str(t, "\b \b");
        }
        return;
    }
    if (c >= 32 && c < 127) {
        if (t->line_len < TTY_LINE_MAX - 1) {
            t->line_buf[t->line_len++] = c;
            if (t->echo) tty_render_char(t, c);
        }
    }
}

void tty_poll_keyboard(void) {
    struct tty* t = tty_get_active();
    while (keyboard_has_input()) {
        char c = keyboard_getchar();
        if (c) tty_handle_char(t, c);
    }
}

void tty_inject(struct tty* t, const char* data, uint32_t len) {
    if (!t || !data) return;
    /* No tty_lock here: chars may trigger a session switch whose banner
     * write re-enters tty_write (same lock -> deadlock). This matches
     * tty_poll_keyboard, which also dispatches unlocked; injection runs
     * from kernel code before the scheduler starts. */
    for (uint32_t i = 0; i < len; i++) {
        tty_handle_char(t, data[i]);
    }
}

int tty_read(struct tty* tty, char* buffer, uint32_t max_len, uint64_t timeout_ticks) {
    if (!tty || !buffer || max_len == 0) return -1;

    uint64_t deadline = pit_get_ticks() + timeout_ticks;
    for (;;) {
        tty_poll_keyboard();

        uint32_t n = 0;
        spin_lock(&tty_lock);
        if (tty->input_count > 0) {
            if (tty->mode == TTY_MODE_RAW) {
                uint32_t slot = tty->input_head;
                buffer[0] = tty->input[slot][0];
                n = 1;
                tty->input_head = (uint8_t)((tty->input_head + 1) % TTY_INPUT_LINES);
                tty->input_count--;
            } else {
                uint32_t got = 0;
                if (input_pop_line(tty, buffer, max_len, &got)) {
                    if (got < max_len) buffer[got++] = '\n';
                    n = got;
                }
            }
        }
        spin_unlock(&tty_lock);
        if (n > 0) return (int)n;

        if (pit_get_ticks() >= deadline) {
            kprintf("tty: read EOF after %lu ticks (pid %u)\n",
                    pit_get_ticks(), process_get_current() ? process_get_current()->pid : 0);
            return 0; /* EOF on timeout */
        }

        scheduler_yield();
    }
}
