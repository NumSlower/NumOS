/*
 * shell.c — NumOS interactive shell
 *
 * Language runtime support
 * ========================
 * The shell can launch interpreted scripts by routing them through an
 * interpreter ELF. Two detection mechanisms are supported:
 *
 *   1. File extension — maps to /bin/EXT.ELF.
 *      EXT is the upper-case extension without the dot.
 *
 *   2. Shebang line — if the first two bytes of the file are "#!",
 *      the rest of the first line is used as the interpreter path.
 *
 * The shell calls sys_exec_argv(interpreter_path, script_path).
 * The interpreter reads its script path with sys_get_cmdline(buf, len).
 *
 * Neither the kernel nor this file names a specific language.
 *
 * Adding a new language
 * ---------------------
 *   1. Cross-compile your interpreter to a NumOS ELF (see docs/).
 *   2. Name it EXT.ELF and place it in build/user/.
 *      create_disk.py will put it at /bin/EXT.ELF on the disk.
 *   3. Rebuild.
 *
 * The interpreter itself calls sys_get_cmdline(buf, len) to receive the
 * script path, then opens and interprets the file via sys_open/sys_read.
 */

#include "syscalls.h"
#include "program_version.h"
#include <stddef.h>

/* =========================================================================
 * Interpreter resolution
 *
 * Extension-based mapping follows a naming rule:
 *   script.ext  ->  /bin/EXT.ELF
 *
 * EXT is the extension without the dot, upper-case, 1 to 8 characters.
 * ========================================================================= */

/* =========================================================================
 * String utilities (no libc in user space)
 * ========================================================================= */

static size_t str_len(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static void write_str(const char *s) {
    sys_write(FD_STDOUT, s, str_len(s));
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void read_token(const char *s, char *out, size_t cap,
                       const char **rest) {
    size_t i = 0;
    while (s[i] && s[i] != ' ' && s[i] != '\t') {
        if (i + 1 < cap) out[i] = s[i];
        i++;
    }
    out[(i < cap) ? i : cap - 1] = '\0';
    if (rest) *rest = skip_spaces(s + i);
}

static int has_char(const char *s, char ch) {
    while (*s) { if (*s == ch) return 1; s++; }
    return 0;
}

static struct fat32_dirent shell_dir_entries[64];

static char ascii_upper(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

static int build_prefixed_path(char *out, size_t cap, const char *prefix,
                               const char *name, const char *suffix,
                               int uppercase_name) {
    size_t pos = 0;

    if (!out || cap == 0 || !prefix || !name) return 0;
    out[0] = '\0';

    while (*prefix) {
        if (pos + 1 >= cap) return 0;
        out[pos++] = *prefix++;
    }

    while (*name) {
        char ch = *name++;
        if (pos + 1 >= cap) return 0;
        out[pos++] = uppercase_name ? ascii_upper(ch) : ch;
    }

    if (suffix) {
        while (*suffix) {
            if (pos + 1 >= cap) return 0;
            out[pos++] = *suffix++;
        }
    }

    out[pos] = '\0';
    return 1;
}

static int is_empty_or_now(const char *s) {
    if (!s || *s == '\0') return 1;
    return str_eq(s, "now");
}

/* =========================================================================
 * File helpers
 * ========================================================================= */

static int file_exists(const char *path) {
    int fd = (int)sys_open(path, FAT32_O_RDONLY, 0);
    if (fd < 0) return 0;
    sys_close(fd);
    return 1;
}

/*
 * read_first_line — read up to len-1 bytes of the first line of path.
 * Returns the number of characters stored (0 on any error).
 * The buffer is always NUL-terminated.
 */
static size_t read_first_line(const char *path, char *buf, size_t len) {
    if (!buf || len == 0) return 0;
    buf[0] = '\0';

    int fd = (int)sys_open(path, FAT32_O_RDONLY, 0);
    if (fd < 0) return 0;

    size_t n = 0;
    char   c;
    while (n < len - 1) {
        int64_t got = sys_read(fd, &c, 1);
        if (got <= 0) break;
        if (c == '\n' || c == '\r') break;
        buf[n++] = c;
    }
    buf[n] = '\0';
    sys_close(fd);
    return n;
}

/* =========================================================================
 * Language detection and routing
 * ========================================================================= */

/*
 * get_extension — return a pointer to the file extension within path,
 * or NULL if none exists after the last path separator.
 */
static const char *get_extension(const char *path) {
    const char *dot = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '.' ) { dot = p; }
        if (*p == '/' || *p == '\\') { dot = NULL; }  /* reset on separator */
    }
    return dot;
}

/*
 * build_interp_path_from_ext — build /bin/EXT.ELF from ".ext".
 * Returns 1 on success, 0 on failure.
 */
static int build_interp_path_from_ext(const char *ext, char *out, size_t cap) {
    if (!ext || ext[0] != '.') return 0;

    const char *raw = ext + 1;
    size_t ext_len = str_len(raw);
    if (ext_len == 0 || ext_len > 8) return 0;

    size_t needed = 5 + ext_len + 4 + 1; /* "/bin/" + EXT + ".ELF" + NUL */
    if (cap < needed) return 0;

    const char *prefix = "/bin/";
    const char *suffix = ".ELF";
    size_t pos = 0;

    for (size_t i = 0; prefix[i] && pos + 1 < cap; i++) {
        out[pos++] = prefix[i];
    }

    for (size_t i = 0; raw[i] && pos + 1 < cap; i++) {
        char c = raw[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return 0;
        out[pos++] = c;
    }

    for (size_t i = 0; suffix[i] && pos + 1 < cap; i++) {
        out[pos++] = suffix[i];
    }

    out[pos] = '\0';
    return 1;
}

/*
 * find_interpreter_for_ext — resolve the interpreter path for a file extension.
 * Returns 1 if a matching interpreter exists on disk, 0 otherwise.
 */
static int find_interpreter_for_ext(const char *ext, char *out, size_t cap) {
    if (!build_interp_path_from_ext(ext, out, cap)) return 0;
    if (!file_exists(out)) return 0;
    return 1;
}

/*
 * try_run_as_script — attempt to execute path as an interpreted script.
 *
 * Detection order:
 *   1. Shebang: if first line starts with "#!", use the rest as the
 *      interpreter path.
 *   2. Extension: map to /bin/EXT.ELF.
 *
 * Returns 0 if the interpreter was launched, -1 if no interpreter was found.
 */
static int try_run_as_script(const char *path) {
    /* --- Shebang detection --- */
    char first_line[256];
    if (read_first_line(path, first_line, sizeof(first_line)) > 1 &&
        first_line[0] == '#' && first_line[1] == '!') {

        const char *interp = skip_spaces(first_line + 2);
        /* Shebang path must not be empty and the ELF must exist */
        if (interp[0] != '\0' && file_exists(interp)) {
            int64_t rc = sys_exec_argv(interp, path);
            return (rc >= 0) ? 0 : -1;
        }
    }

    /* --- Extension-based lookup --- */
    const char *ext = get_extension(path);
    char interp[64];
    if (find_interpreter_for_ext(ext, interp, sizeof(interp))) {
        int64_t rc = sys_exec_argv(interp, path);
        return (rc >= 0) ? 0 : -1;
    }

    return -1;  /* no interpreter found */
}

/*
 * Resolve a script or program name and attempt to run it.
 * Resolution order:
 *   1. If the name contains '/', treat it as an absolute or relative path.
 *   2. If the name has no extension, try /bin/<NAME>.ELF.
 *   3. Otherwise, use the current directory.
 */
static int try_script_or_exec(const char *path, const char *cmdline) {
    if (!file_exists(path)) return -1;
    if (try_run_as_script(path) == 0) return 0;
    if (sys_exec_argv(path, cmdline) >= 0) return 0;
    return -1;
}

static int try_exec_or_script(const char *cmd, const char *cmdline) {
    char path[128];
    const char *line = (cmdline && cmdline[0]) ? cmdline : "";

    if (has_char(cmd, '/')) {
        return (try_script_or_exec(cmd, line) == 0) ? 0 : -1;
    }

    if (!has_char(cmd, '.')) {
        if (build_prefixed_path(path, sizeof(path), "/bin/", cmd, ".ELF", 1) &&
            file_exists(path) && sys_exec_argv(path, line) >= 0) return 0;
    }

    if (try_script_or_exec(cmd, line) == 0) return 0;

    if (has_char(cmd, '.')) {
        const char *prefixes[] = { "/bin/", "/run/" };
        for (size_t p = 0; p < sizeof(prefixes) / sizeof(prefixes[0]); p++) {
            if (build_prefixed_path(path, sizeof(path), prefixes[p], cmd, "", 1) &&
                try_script_or_exec(path, line) == 0) return 0;
        }
    }

    if (!has_char(cmd, '.') && !has_char(cmd, '/')) {
        const char *suffixes[] = { ".elf", ".ELF" };
        for (size_t sfx = 0; sfx < sizeof(suffixes) / sizeof(suffixes[0]); sfx++) {
            const char *suffix = suffixes[sfx];
            size_t pos = 0;
            for (size_t i = 0; cmd[i] && pos < sizeof(path) - 1; i++)
                path[pos++] = cmd[i];
            for (size_t i = 0; suffix[i] && pos < sizeof(path) - 1; i++)
                path[pos++] = suffix[i];
            path[pos] = '\0';
            if (try_script_or_exec(path, line) == 0) return 0;
        }
    }

    return -1;
}

static int try_exec_or_script_run(const char *cmd, const char *cmdline) {
    char path[128];
    const char *line = (cmdline && cmdline[0]) ? cmdline : "";

    if (has_char(cmd, '/')) {
        return (try_script_or_exec(cmd, line) == 0) ? 0 : -1;
    }

    if (has_char(cmd, '.')) {
        if (build_prefixed_path(path, sizeof(path), "/bin/", cmd, "", 1)) {
            return (try_script_or_exec(path, line) == 0) ? 0 : -1;
        }
        return -1;
    }

    {
        if (build_prefixed_path(path, sizeof(path), "/bin/", cmd, ".ELF", 1) &&
            file_exists(path) && sys_exec_argv(path, line) >= 0) return 0;
    }

    {
        if (build_prefixed_path(path, sizeof(path), "/bin/", cmd, "", 1)) {
            return (try_script_or_exec(path, line) == 0) ? 0 : -1;
        }
        return -1;
    }
}

static void write_dec(uint32_t value) {
    char buf[16];
    char tmp[16];
    int pos = 0;
    int t = 0;

    if (value == 0) {
        buf[pos++] = '0';
        sys_write(FD_STDOUT, buf, (size_t)pos);
        return;
    }

    while (value > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (value % 10));
        value /= 10;
    }
    for (int i = t - 1; i >= 0; i--) {
        buf[pos++] = tmp[i];
    }
    sys_write(FD_STDOUT, buf, (size_t)pos);
}

static int is_system_dir_name(const char *path) {
    return str_eq(path, "bin") ||
           str_eq(path, "run") ||
           str_eq(path, "init") ||
           str_eq(path, "include") ||
           str_eq(path, "home");
}

static void list_directory_cmd(const char *path) {
    const char *use_path = (path && path[0]) ? path : "";
    char norm_path[64];
    int64_t count = sys_listdir(use_path, shell_dir_entries, 64);
    char abs_path[64];

    if (use_path[0]) {
        size_t len = str_len(use_path);
        while (len > 1 && use_path[len - 1] == '/') len--;
        if (len != str_len(use_path) && len < sizeof(norm_path)) {
            for (size_t i = 0; i < len; i++) norm_path[i] = use_path[i];
            norm_path[len] = '\0';
            use_path = norm_path;
            count = sys_listdir(use_path, shell_dir_entries, 64);
        }
    }

    if (count < 0 && use_path[0] && use_path[0] != '/' && is_system_dir_name(use_path)) {
        size_t pos = 0;
        size_t i = 0;
        abs_path[pos++] = '/';
        while (use_path[i] && pos < sizeof(abs_path) - 1) {
            abs_path[pos++] = use_path[i++];
        }
        abs_path[pos] = '\0';
        count = sys_listdir(abs_path, shell_dir_entries, 64);
    }

    if (count < 0) {
        write_str("list failed\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        if (shell_dir_entries[i].attr & FAT32_ATTR_DIRECTORY) {
            write_str("[DIR]  ");
        } else {
            write_str("[FILE] ");
        }
        write_str(shell_dir_entries[i].name);
        if (!(shell_dir_entries[i].attr & FAT32_ATTR_DIRECTORY)) {
            write_str(" ");
            write_dec(shell_dir_entries[i].size);
            write_str(" bytes");
        }
        write_str("\n");
    }
}

/* =========================================================================
 * Built-in commands
 * ========================================================================= */

static void clear_screen(void) { sys_fb_clear(); }
static void prompt(void)       { write_str("> "); }

static void print_help(void) {
    write_str("built-in commands:\n");
    write_str("  exit         exit the shell\n");
    write_str("  reboot       reboot the system\n");
    write_str("  shutdown     power off the system\n");
    write_str("  poweroff     power off the system\n");
    write_str("  clear        clear the screen\n");
    write_str("  scroll       enter console scrollback mode\n");
    write_str("  help         show this help\n");
    write_str("  lang         show interpreter rule\n");
    write_str("  install ata  write a bootable NumOS system to the primary ATA disk\n");
    write_str("  pkg kernel <path|URL> [reboot]  stage a new /boot kernel with fallback\n");
    write_str("  run          list or run programs in /bin/\n");
    write_str("  list         list directory entries\n");
    write_str("\nbundled tools:\n");
    write_str("  mk           run targets from /home/BUILD.MK or another build file\n");
    write_str("  numloss      compress or decompress files with NMLS archives\n");
    write_str("  empty        print text files safely\n");
    write_str("  pkg          install packages and stage kernels\n");
    write_str("  connect      inspect networking, TCP, HTTP, TLS, and HTTPS\n");
    write_str("  tcp          legacy IPv4 TCP and HTTP tool\n");
    write_str("  see          send ICMP echo requests to an IPv4 host\n");
    write_str("  usb          inspect USB controllers and ports\n");
    write_str("\nrunning programs and scripts:\n");
    write_str("  <name>       run /bin/<NAME>.ELF\n");
    write_str("  <file>       run file in current directory\n");
    write_str("  <file.ext>   run script in current directory via /bin/EXT.ELF\n");
    write_str("  /path/file   run ELF or script at an explicit path\n");
    write_str("  #!/bin/x     scripts with a shebang use that interpreter\n");
}

/* Print interpreter rule */
static void print_lang_registry(void) {
    write_str("interpreter rule:\n");
    write_str("  file.ext uses /bin/EXT.ELF\n");
    write_str("  EXT is the upper-case extension without the dot\n");
    write_str("  shebang lines override the rule\n");
    write_str("\nplace interpreter ELFs in build/user/ and rebuild.\n");
}

/* =========================================================================
 * Command dispatcher
 * ========================================================================= */

static int handle_command(const char *line) {
    const char *s = skip_spaces(line);
    if (*s == '\0') return 0;  /* empty line */

    char        cmd[64];
    const char *args = NULL;
    read_token(s, cmd, sizeof(cmd), &args);
    if (cmd[0] == '\0') return 0;

    /* ---- Built-in: exit ---- */
    if (str_eq(cmd, "exit")) {
        sys_exit(0);
        return 1;
    }

    /* ---- Built-in: reboot / shutdown ---- */
    if (str_eq(cmd, "reboot")) { sys_reboot(); return 1; }
    if (str_eq(cmd, "poweroff")) {
        sys_poweroff();
        return 1;
    }
    if (str_eq(cmd, "shutdown")) {
        if (is_empty_or_now(args) || str_eq(args, "-h") || str_eq(args, "-h now")) {
            sys_poweroff();
            return 1;
        }
        if (str_eq(args, "-r") || str_eq(args, "-r now")) {
            sys_reboot();
            return 1;
        }
        write_str("usage: shutdown\n");
        write_str("       shutdown now\n");
        write_str("       shutdown -h\n");
        write_str("       shutdown -h now\n");
        write_str("       shutdown -r\n");
        write_str("       shutdown -r now\n");
        return 1;
    }

    /* ---- Built-in: clear ---- */
    if (str_eq(cmd, "clear")) {
        clear_screen();
        prompt();
        return 1;
    }

    /* ---- Built-in: scroll ---- */
    if (str_eq(cmd, "scroll")) {
        sys_con_scroll();
        return 0;
    }

    /* ---- Built-in: help ---- */
    if (str_eq(cmd, "help")) {
        print_help();
        return 0;
    }

    /* ---- Built-in: lang — show interpreter rule ---- */
    if (str_eq(cmd, "lang")) {
        print_lang_registry();
        return 0;
    }

    /* ---- Built-in: list / dir ---- */
    if (str_eq(cmd, "list") || str_eq(cmd, "dir")) {
        list_directory_cmd(args);
        return 0;
    }

    /* ---- Built-in: run [name] ---- */
    if (str_eq(cmd, "run") || str_eq(cmd, "/bin") || str_eq(cmd, "/bin/")) {
        if (!args || args[0] == '\0') {
            /* No argument — just print info */
            write_str("programs in /bin/ can be launched by typing their name.\n");
            write_str("type 'lang' for the interpreter rule.\n");
        } else {
            if (try_exec_or_script_run(args, "") != 0) {
                sys_write(FD_STDOUT, "not found: ", 11);
                write_str(args);
                write_str("\n");
            }
        }
        return 0;
    }

    /* ---- Anything else: try to run it ---- */
    if (try_exec_or_script(cmd, args) != 0) {
        sys_write(FD_STDOUT, "unknown command: ", 17);
        write_str(cmd);
        write_str("\n");
    }
    return 0;
}

/* =========================================================================
 * Main loop
 * ========================================================================= */

#define IBUF_SIZE 256

int main(int argc, char **argv) {
    char   buf[IBUF_SIZE];
    size_t len = 0;

    if (argc >= 2 && numos_is_version_flag(argv[1])) {
        numos_print_program_version("shell");
        return 0;
    }

    write_str("\n");
    prompt();

    for (;;) {
        char c = '\0';
        if (sys_input(&c, 1) <= 0) continue;
        if (c == '\r') c = '\n';

        if (c == KEY_SPECIAL_UP || c == KEY_SPECIAL_DOWN) {
            sys_con_scroll();
            continue;
        }

        if (c == '\n') {
            buf[len] = '\0';
            write_str("\n");
            int handled = handle_command(buf);
            len = 0;
            if (!handled) prompt();
            continue;
        }

        if (c == '\b' || c == 0x7F) {
            if (len > 0) {
                len--;
                write_str("\b \b");
            }
            continue;
        }

        if (len + 1 < IBUF_SIZE) {
            buf[len++] = c;
            sys_write(FD_STDOUT, &c, 1);
        }
    }

    return 0;
}
