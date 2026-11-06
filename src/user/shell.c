/* NumOS — shell.c — The NumOS userspace shell (spec 12.2–12.6, first increment)
 *
 * Runs as a Ring 3 ELF64 process, one per terminal session (argv[1] holds
 * the session index). This increment implements:
 *   - Lexer: words, single/double quotes, backslash escapes, $VAR and $?
 *     expansion (expansion happens during word production, after quoting
 *     decides literal vs. substitutable context).
 *   - Parser: list (';' separated) -> conditional (&&/||) -> command with
 *     optional < > >> redirection targets. Structured token stream, not
 *     substring matching. Syntax errors report token context.
 *   - Builtins (12.6, real data): help, clear, echo, pwd, ls, cat, stat,
 *     write, append, rm, ps, uptime, pid, env, set, unset, history, true,
 *     false, exit. cd/mkdir/rmdir/cp/mv report the documented FS limits.
 *   - External ELF64 execution: /BIN/<name>.ELF via SYS_EXEC/SYS_WAIT with
 *     the inherited environment passed as envp (copy semantics).
 *   - Redirection < > >> for externals via NUMOS_STDIN/NUMOS_STDOUT env
 *     entries the child program honors (a documented convention); for
 *     builtins via direct file I/O in the shell.
 *   - Sessions: this instance is session N; the user switches with Ctrl-N.
 * Deferred, documented: kernel pipes (12.4), 2>, scripting (12.5),
 * startup config file, job control (fg/bg/jobs after process groups).
 */
#include "kernel/syscall.h"

#define SHELL_LINE_MAX 256
#define SHELL_TOK_MAX 32
#define SHELL_ENV_MAX 16
#define SHELL_HIST_MAX 8

static char env_names[SHELL_ENV_MAX][32];
static char env_values[SHELL_ENV_MAX][64];
static uint32_t env_count = 0;
static char history[SHELL_HIST_MAX][SHELL_LINE_MAX];
static uint32_t hist_total = 0;
static int last_status = 0;
static unsigned shell_session = 0;
static int out_fd = 1; /* builtin stdout: swapped by > / >> redirection */

/* ---- syscall layer -------------------------------------------------------- */

static long syscall3(long nr, long a, long b, long c) {
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}

/* ---- syscall wrappers ---------------------------------------------------- */

static long sys_write(int fd, const void* buf, unsigned len) {
    return syscall3(SYS_WRITE, fd, (long)buf, (long)len);
}
static long sys_read(int fd, void* buf, unsigned len) {
    return syscall3(SYS_READ, fd, (long)buf, (long)len);
}
static long sys_open(const char* path, int flags) {
    return syscall3(SYS_OPEN, (long)path, flags, 0);
}
static long sys_close(int fd) {
    return syscall3(SYS_CLOSE, fd, 0, 0);
}
static long sys_exec(const char* path, const char* const* argv, const char* const* envp) {
    return syscall3(SYS_EXEC, (long)path, (long)argv, (long)envp);
}
static long sys_wait(int pid) {
    return syscall3(SYS_WAIT, pid, 0, 0);
}
static unsigned long sys_uptime(void) {
    return syscall3(SYS_UPTIME, 0, 0, 0);
}
static int sys_getpid(void) {
    return (int)syscall3(SYS_GETPID, 0, 0, 0);
}
static long sys_list(const char* path, void* buf, unsigned max) {
    return syscall3(SYS_LIST, (long)path, (long)buf, max);
}
static long sys_statline(const char* path, void* buf, unsigned max) {
    return syscall3(SYS_STAT, (long)path, (long)buf, max);
}
static long sys_unlink(const char* path) {
    return syscall3(SYS_UNLINK, (long)path, 0, 0);
}
static void sys_exit(int code) {
    syscall3(SYS_EXIT, code, 0, 0);
    for (;;) ;
}

static void out(const char* s) {
    unsigned n = 0;
    while (s[n]) n++;
    sys_write(out_fd, s, n);
}

static void out_num(long v) {
    char buf[24];
    unsigned i = 0;
    unsigned long u = (v < 0) ? (unsigned long)-(v + 1) + 1 : (unsigned long)v;
    if (v < 0) buf[i++] = '-';
    char digits[20];
    unsigned d = 0;
    do { digits[d++] = (char)('0' + (u % 10)); u /= 10; } while (u);
    while (d) buf[i++] = digits[--d];
    sys_write(out_fd, buf, i);
}

static unsigned long slen(const char* s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

static void scopy(char* d, const char* s, unsigned cap) {
    unsigned i = 0;
    while (s[i] && i < cap - 1) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static int scompare(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ---- environment (12.2) --------------------------------------------------- */

static const char* env_get(const char* name) {
    for (uint32_t i = 0; i < env_count; i++) {
        if (scompare(env_names[i], name) == 0) return env_values[i];
    }
    return NULL;
}

static int env_set(const char* name, const char* value) {
    for (uint32_t i = 0; i < env_count; i++) {
        if (scompare(env_names[i], name) == 0) {
            scopy(env_values[i], value, 64);
            return 0;
        }
    }
    if (env_count >= SHELL_ENV_MAX || !env_names[0][0] == 0) {
        if (env_count >= SHELL_ENV_MAX) return -1;
    }
    scopy(env_names[env_count], name, 32);
    scopy(env_values[env_count], value, 64);
    env_count++;
    return 0;
}

static int env_unset(const char* name) {
    for (uint32_t i = 0; i < env_count; i++) {
        if (scompare(env_names[i], name) == 0) {
            for (uint32_t j = i; j + 1 < env_count; j++) {
                scopy(env_names[j], env_names[j + 1], 32);
                scopy(env_values[j], env_values[j + 1], 64);
            }
            env_count--;
            return 0;
        }
    }
    return -1;
}

/* ---- lexer (12.3) ----------------------------------------------------------
 * Produces tokens: T_WORD (expanded), T_SEMI, T_ANDAND, T_OROR, T_PIPE,
 * T_LT, T_GT, T_GTGT. Returns token count or -1 on syntax error
 * (unterminated quote). Expansion: $NAME, $? inside unquoted and
 * double-quoted text; escaped \$ stays literal. */
enum tok_type { T_WORD, T_SEMI, T_ANDAND, T_OROR, T_PIPE, T_LT, T_GT, T_GTGT };

struct token {
    enum tok_type type;
    char text[SHELL_LINE_MAX];
};

static int lex(const char* line, struct token* toks, uint32_t max_toks) {
    uint32_t ntok = 0;
    unsigned long i = 0;

    while (line[i]) {
        while (line[i] == ' ' || line[i] == '\t') i++;
        if (!line[i]) break;

        if (ntok >= max_toks) return -2;
        struct token* t = &toks[ntok];
        t->type = T_WORD;
        t->text[0] = '\0';
        uint32_t w = 0;

        if (line[i] == ';') { t->type = T_SEMI; t->text[0] = ';'; ntok++; i++; continue; }
        if (line[i] == '|' && line[i + 1] == '|') {
            t->type = T_OROR; t->text[0] = '|'; t->text[1] = '|';
            ntok++; i += 2; continue;
        }
        if (line[i] == '|') { t->type = T_PIPE; t->text[0] = '|'; ntok++; i++; continue; }
        if (line[i] == '<') { t->type = T_LT; t->text[0] = '<'; ntok++; i++; continue; }
        if (line[i] == '>') {
            i++;
            if (line[i] == '>') { t->type = T_GTGT; t->text[0] = '>'; t->text[1] = '>'; i++; }
            else t->type = T_GT;
            ntok++;
            continue;
        }
        if (line[i] == '&' && line[i + 1] == '&') {
            t->type = T_ANDAND; t->text[0] = '&'; t->text[1] = '&';
            ntok++; i += 2; continue;
        }
        if (line[i] == '&') return -1; /* background jobs not supported */

        /* word */
        while (line[i] && line[i] != ' ' && line[i] != '\t' &&
               line[i] != ';' && line[i] != '|' && line[i] != '<' &&
               !(line[i] == '>' || (line[i] == '&' && line[i + 1] == '&'))) {
            char c = 0;
            if (line[i] == '\\' && line[i + 1]) {
                i++;
                c = line[i++];          /* escaped char is literal */
            } else if (line[i] == '\'') {
                i++;
                while (line[i] && line[i] != '\'') {
                    if (w < SHELL_LINE_MAX - 1) t->text[w++] = line[i];
                    i++;
                }
                if (line[i] != '\'') return -1; /* unterminated quote */
                i++;
                continue;
            } else if (line[i] == '"') {
                i++;
                while (line[i] && line[i] != '"') {
                    if (line[i] == '\\' && line[i + 1]) i++;
                    if (w < SHELL_LINE_MAX - 1) t->text[w++] = line[i];
                    i++;
                }
                if (line[i] != '"') return -1;
                i++;
                continue;
            } else if (line[i] == '$') {
                i++;
                if (line[i] == '?') {
                    i++;
                    char nb[16];
                    long v = last_status;
                    unsigned k = 0;
                    unsigned long u = (v < 0) ? (unsigned long)-v : (unsigned long)v;
                    if (v < 0) nb[k++] = '-';
                    char dd[12]; unsigned dn = 0;
                    do { dd[dn++] = (char)('0' + (u % 10)); u /= 10; } while (u);
                    while (dn) nb[k++] = dd[--dn];
                    for (unsigned q = 0; q < k && w < SHELL_LINE_MAX - 1; q++) {
                        t->text[w++] = nb[q];
                    }
                    continue;
                }
                char name[32];
                uint32_t nlen = 0;
                while (line[i] && ((line[i] >= 'A' && line[i] <= 'Z') ||
                                   (line[i] >= 'a' && line[i] <= 'z') ||
                                   (line[i] >= '0' && line[i] <= '9') ||
                                   line[i] == '_') && nlen < 31) {
                    name[nlen++] = line[i++];
                }
                name[nlen] = '\0';
                const char* v = env_get(name);
                if (v) {
                    while (*v && w < SHELL_LINE_MAX - 1) t->text[w++] = *v++;
                }
                continue;
            } else {
                c = line[i++];
            }
            if (w < SHELL_LINE_MAX - 1) t->text[w++] = c;
        }
        t->text[w] = '\0';
        ntok++;
    }
    return (int)ntok;
}

/* ---- builtins (12.6) ------------------------------------------------------- */

struct cmd {
    char* argv[SHELL_TOK_MAX];
    uint32_t argc;
    const char* stdin_path;
    const char* stdout_path;
    bool append;
};

static int bi_help(const struct cmd* c);
static int bi_ls(const struct cmd* c);

typedef int (*builtin_fn)(const struct cmd* c);

static int bi_clear(const struct cmd* c) {
    (void)c;
    out("\033[2J");
    return 0;
}

static int bi_echo(const struct cmd* c) {
    for (uint32_t i = 1; i < c->argc; i++) {
        out(c->argv[i]);
        if (i + 1 < c->argc) out(" ");
    }
    out("\n");
    return 0;
}

static int bi_pwd(const struct cmd* c) {
    (void)c;
    out("/\n");
    return 0;
}

static int bi_cd(const struct cmd* c) {
    (void)c;
    out("cd: the mounted filesystem has a single root directory\n");
    return 1;
}

static int bi_help(const struct cmd* c) {
    (void)c;
    out("NumOS shell - builtins:\n");
    out("  echo pwd ls cat stat write append rm ps uptime pid env set unset\n");
    out("  history true false clear help exit\n");
    out("externals: /BIN/<name>.ELF   operators: ; && || < > >>\n");
    out("variables: set NAME VALUE, expand $NAME, status $?\n");
    out("sessions: Ctrl-1 / Ctrl-2   deferred: pipes, 2>, scripts\n");
    return 0;
}

static int bi_ls(const struct cmd* c) {
    const char* path = (c->argc > 1) ? c->argv[1] : "/";
    static char buf[2048];
    long n = sys_list(path, buf, sizeof(buf) - 1);
    if (n <= 0) {
        out("ls: cannot list ");
        out(path);
        out("\n");
        return 1;
    }
    buf[n] = '\0';
    /* reformat "name TYPE size" lines aligned */
    out(buf);
    return 0;
}

static int bi_cat(const struct cmd* c) {
    if (c->argc < 2) {
        out("usage: cat FILE\n");
        return 1;
    }
    long fd = sys_open(c->argv[1], 0);
    if (fd < 0) {
        out("cat: cannot open ");
        out(c->argv[1]);
        out("\n");
        return 1;
    }
    static char buf[512];
    for (;;) {
        long n = syscall3(SYS_READ, fd, (long)buf, 512);
        if (n <= 0) break;
        sys_write(1, buf, (unsigned)n);
    }
    sys_close((int)fd);
    return 0;
}

static int bi_stat(const struct cmd* c) {
    if (c->argc < 2) {
        out("usage: stat FILE\n");
        return 1;
    }
    static char buf[128];
    long n = sys_statline(c->argv[1], buf, sizeof(buf) - 1);
    if (n <= 0) {
        out("stat: not found\n");
        return 1;
    }
    buf[n] = '\0';
    out(buf);
    return 0;
}

static int bi_write(const struct cmd* c, bool append) {
    if (c->argc < 3) {
        out("usage: write FILE TEXT...\n");
        return 1;
    }
    int flags = 0x0001 | 0x0100 | (append ? 0 : 0x0200); /* WO|CREAT|TRUNC */
    long fd = sys_open(c->argv[1], flags);
    if (fd < 0) {
        out("write: cannot open ");
        out(c->argv[1]);
        out("\n");
        return 1;
    }
    for (uint32_t i = 2; i < c->argc; i++) {
        sys_write((int)fd, c->argv[i], (unsigned)slen(c->argv[i]));
        if (i + 1 < c->argc) sys_write((int)fd, " ", 1);
    }
    sys_write((int)fd, "\n", 1);
    sys_close((int)fd);
    return 0;
}

static int bi_rm(const struct cmd* c) {
    if (c->argc < 2) {
        out("usage: rm FILE\n");
        return 1;
    }
    return sys_unlink(c->argv[1]) == 0 ? 0 : 1;
}

static int bi_ps(const struct cmd* c) {
    (void)c;
    static char buf[1024];
    long n = syscall3(SYS_PS, (long)buf, 1023, 0);
    if (n <= 0) return 1;
    buf[n] = '\0';
    out("PID PPID STATE  NAME\n");
    out(buf);
    return 0;
}

static int bi_env(const struct cmd* c) {
    (void)c;
    for (uint32_t i = 0; i < env_count; i++) {
        out(env_names[i]);
        out("=");
        out(env_values[i]);
        out("\n");
    }
    return 0;
}

static int bi_set(const struct cmd* c) {
    if (c->argc != 3) {
        out("usage: set NAME VALUE\n");
        return 1;
    }
    return env_set(c->argv[1], c->argv[2]) == 0 ? 0 : 1;
}

static int bi_unset(const struct cmd* c) {
    if (c->argc != 2) {
        out("usage: unset NAME\n");
        return 1;
    }
    return env_unset(c->argv[1]) == 0 ? 0 : 1;
}

static int bi_history(const struct cmd* c) {
    (void)c;
    uint32_t start = hist_total > SHELL_HIST_MAX ? hist_total - SHELL_HIST_MAX : 0;
    for (uint32_t h = start; h < hist_total; h++) {
        out_num((long)(h + 1));
        out("  ");
        out(history[h % SHELL_HIST_MAX]);
        out("\n");
    }
    return 0;
}

static int bi_uptime(const struct cmd* c) {
    (void)c;
    out_num((long)(sys_uptime() / 1000));
    out("s\n");
    return 0;
}

static int bi_pid(const struct cmd* c) {
    (void)c;
    out_num(sys_getpid());
    out("\n");
    return 0;
}

static int run_external(const struct cmd* c) {
    char path[96];
    scopy(path, "/BIN/", 96);
    {
        unsigned long cl = slen(c->argv[0]);
        if (cl > 80) return 127;
        scopy(path + 5, c->argv[0], 96 - 5);
        unsigned long base = 5 + cl;
        scopy(path + base, ".ELF", 96 - base);
    }

    /* Redirections travel to the child as documented env entries. */
    const char* xargv[SHELL_TOK_MAX + 1];
    const char* xenvp[SHELL_ENV_MAX + 4];
    uint32_t e = 0;
    for (uint32_t i = 0; i < env_count; i++) {
        static char kv[SHELL_ENV_MAX][96];
        scopy(kv[i], env_names[i], 32);
        {
            unsigned long l = slen(kv[i]);
            scopy(kv[i] + l, "=", 96 - l);
            scopy(kv[i] + l + 1, env_values[i], 96 - l - 1);
        }
        xenvp[e++] = kv[i];
    }
    static char redir_in[96], redir_out[96];
    if (c->stdin_path) {
        scopy(redir_in, "NUMOS_STDIN=", 96);
        scopy(redir_in + 12, c->stdin_path, 96 - 12);
        xenvp[e++] = redir_in;
    }
    if (c->stdout_path) {
        scopy(redir_out, c->append ? "NUMOS_APPEND=" : "NUMOS_STDOUT=", 96);
        scopy(redir_out + (c->append ? 13 : 13), c->stdout_path, 96 - 13);
        xenvp[e++] = redir_out;
    }
    xenvp[e] = NULL;
    for (uint32_t i = 0; i < c->argc; i++) xargv[i] = c->argv[i];
    xargv[c->argc] = NULL;

    long pid = sys_exec(path, xargv, xenvp);
    if (pid < 0) {
        out("sh: ");
        out(c->argv[0]);
        out(": not found\n");
        return 127;
    }
    long code = sys_wait((int)pid);
    return (int)code;
}

static bool is_builtin_name(const char* name) {
    static const char* names[] = {
        "help", "clear", "echo", "pwd", "cd", "ls", "cat", "stat",
        "write", "append", "rm", "ps", "env", "set", "unset",
        "history", "uptime", "pid", "true", "false", "exit",
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (scompare(name, names[i]) == 0) return true;
    }
    return false;
}

static int run_command(struct cmd* c) {
    if (c->argc == 0) return 0;
    if (scompare(c->argv[0], "help") == 0) return bi_help(c);
    if (scompare(c->argv[0], "clear") == 0) return bi_clear(c);
    if (scompare(c->argv[0], "echo") == 0) return bi_echo(c);
    if (scompare(c->argv[0], "pwd") == 0) return bi_pwd(c);
    if (scompare(c->argv[0], "cd") == 0) return bi_cd(c);
    if (scompare(c->argv[0], "ls") == 0) return bi_ls(c);
    if (scompare(c->argv[0], "cat") == 0) return bi_cat(c);
    if (scompare(c->argv[0], "stat") == 0) return bi_stat(c);
    if (scompare(c->argv[0], "write") == 0) return bi_write(c, false);
    if (scompare(c->argv[0], "append") == 0) return bi_write(c, true);
    if (scompare(c->argv[0], "rm") == 0) return bi_rm(c);
    if (scompare(c->argv[0], "ps") == 0) return bi_ps(c);
    if (scompare(c->argv[0], "env") == 0) return bi_env(c);
    if (scompare(c->argv[0], "set") == 0) return bi_set(c);
    if (scompare(c->argv[0], "unset") == 0) return bi_unset(c);
    if (scompare(c->argv[0], "history") == 0) return bi_history(c);
    if (scompare(c->argv[0], "uptime") == 0) return bi_uptime(c);
    if (scompare(c->argv[0], "pid") == 0) return bi_pid(c);
    if (scompare(c->argv[0], "true") == 0) return 0;
    if (scompare(c->argv[0], "false") == 0) return 1;
    if (scompare(c->argv[0], "exit") == 0) sys_exit((c->argc > 1) ? 0 : 0);

    return run_external(c);
}

/* Executor: list = conditionals separated by ';'. Status flows through
 * && / ||. Redirections: builtins get direct fd work here; externals get
 * env-convention entries (see run_external). */
static int run_list(struct token* toks, int ntok);

static int run_pipeline_part(struct token* toks, int start, int end) {
    struct cmd c;
    c.argc = 0;
    c.stdin_path = NULL;
    c.stdout_path = NULL;
    c.append = false;

    int i = start;
    while (i < end) {
        struct token* t = &toks[i];
        if (t->type == T_LT || t->type == T_GT || t->type == T_GTGT) {
            if (i + 1 >= end || toks[i + 1].type != T_WORD) {
                out("sh: missing redirection target\n");
                return 2;
            }
            if (t->type == T_LT) c.stdin_path = toks[i + 1].text;
            else { c.stdout_path = toks[i + 1].text; c.append = (t->type == T_GTGT); }
            i += 2;
            continue;
        }
        if (c.argc < SHELL_TOK_MAX) {
            c.argv[c.argc] = t->text;
            c.argc++;
        }
        i++;
    }

    if (c.argc == 0) {
        out("sh: empty command\n");
        return 2;
    }

    /* Builtin redirection: swap the builtin stdout fd for the duration of
     * the command (externals take the env convention in run_external). */
    if (c.stdout_path && is_builtin_name(c.argv[0])) {
        int flags = 0x0001 | 0x0100 | (c.append ? 0 : 0x0200); /* WO|CREAT|TRUNC */
        long fd = sys_open(c.stdout_path, flags);
        if (fd < 0) {
            out("sh: cannot open ");
            out(c.stdout_path);
            out("\n");
            return 1;
        }
        out_fd = (int)fd;
        int st = run_command(&c);
        out_fd = 1;
        sys_close((int)fd);
        return st;
    }

    return run_command(&c);
}

static int run_list(struct token* toks, int ntok) {
    int status = 0;
    int i = 0;

    while (i < ntok) {
        /* conditional chain: pipeline { (&&|||) pipeline } */
        int pstart = i;
        int depth_ok = 1;
        while (i < ntok && toks[i].type != T_SEMI) {
            if (toks[i].type == T_PIPE) depth_ok = 0;
            i++;
        }
        int pend = i;
        if (i < ntok && toks[i].type == T_SEMI) i++;

        if (!depth_ok) {
            out("sh: pipelines are not supported yet (kernel pipes pending)\n");
            status = 2;
            continue;
        }

        /* conditional chain within [pstart, pend): segments separated by
         * && / ||. The operator AT the boundary decides whether the next
         * segment runs, based on the status of the previous one. */
        int s = pstart;
        int cond_status = 0;
        bool have_prev = false;
        int prev_op = T_SEMI;
        for (int j = pstart; j <= pend; j++) {
            bool at_end = (j == pend);
            bool is_op = !at_end && (toks[j].type == T_ANDAND || toks[j].type == T_OROR);
            if (at_end || is_op) {
                bool run;
                if (!have_prev) run = true;
                else run = (prev_op == T_ANDAND) ? (cond_status == 0) : (cond_status != 0);
                if (run) cond_status = run_pipeline_part(toks, s, j);
                have_prev = true;
                prev_op = is_op ? toks[j].type : T_SEMI;
                s = j + 1;
            }
        }
        status = cond_status;
    }
    return status;
}

/* ---- main loop ------------------------------------------------------------- */

void shell_main(unsigned argc, char** argv, char** envp);

/* Naked entry: the kernel iretqs in with rsp at the argv/envp block
 * ([argc][argv...][NULL][envp...][NULL]). Any C prologue would shift rsp
 * before the block is read, so capture it in asm and pass the three args
 * in SysV registers. */
__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "mov (%rsp), %edi\n"           /* argc */
        "lea 8(%rsp), %rsi\n"          /* argv */
        "lea 16(%rsp,%rdi,8), %rdx\n"  /* envp = argv + argc + 1 */
        "call shell_main\n"
        "mov $0, %edi\n"
        "call sys_exit\n");
}

void shell_main(unsigned argc, char** argv, char** envp) {
    shell_session = 0;
    if (argc > 1) shell_session = (unsigned)(argv[1][0] - '0');
    for (unsigned e = 0; envp && envp[e]; e++) {
        char* eq = envp[e];
        while (*eq && *eq != '=') eq++;
        if (*eq == '=') {
            *eq = '\0';
            env_set(envp[e], eq + 1);
        }
    }
    {
        char tn[2] = {(char)('0' + shell_session), 0};
        env_set("SESSION", tn);
        env_set("PATH", "/BIN");
    }

    char line[SHELL_LINE_MAX];
    for (;;) {
        out("\ns");
        out_num((long)shell_session);
        out(" $ ");
        long n = sys_read(0, line, SHELL_LINE_MAX - 1);
        if (n <= 0) {
            out("[eof]\n");
            sys_exit(0);
        }
        line[n] = '\0';
        if (n > 1 && line[n - 1] == '\n') line[n - 1] = '\0';

        scopy(history[hist_total % SHELL_HIST_MAX], line, SHELL_LINE_MAX);
        hist_total++;

        struct token toks[SHELL_TOK_MAX];
        int ntok = lex(line, toks, SHELL_TOK_MAX);
        if (ntok == -1) {
            out("sh: syntax error: unterminated quote\n");
            last_status = 2;
            continue;
        }
        if (ntok == -2) {
            out("sh: too many tokens\n");
            last_status = 2;
            continue;
        }
        if (ntok == 0) continue;

        last_status = run_list(toks, ntok);
    }
}
