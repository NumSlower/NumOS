/* NumOS — src/user/init.c — First Ring 3 user program (Phase 10)
 *
 * Linked at its own virtual address (0x08000000) by user.ld, converted to
 * a flat binary, and embedded in the kernel image; the kernel maps it into
 * each user process at the same virtual address. It talks to the kernel
 * only through int 0x80 syscalls. Replaced by real ELF64 loading in
 * Phase 11.
 */

#define SYS_READ   0
#define SYS_WRITE  1
#define SYS_OPEN   2
#define SYS_CLOSE  3
#define SYS_EXIT   4
#define SYS_GETPID 5
#define SYS_SBRK   6
#define SYS_YIELD  7
#define SYS_UPTIME 8

static long syscall3(long nr, long a, long b, long c) {
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}

static long sys_write(int fd, const void* buf, unsigned len) {
    return syscall3(SYS_WRITE, fd, (long)buf, (long)len);
}

static long sys_yield(void) {
    return syscall3(SYS_YIELD, 0, 0, 0);
}

static long sys_getpid(void) {
    return syscall3(SYS_GETPID, 0, 0, 0);
}

static long sys_exit(long code) {
    return syscall3(SYS_EXIT, code, 0, 0);
}

static void* sys_sbrk(long delta) {
    return (void*)syscall3(SYS_SBRK, delta, 0, 0);
}

static long sys_read(int fd, void* buf, unsigned len) {
    return syscall3(SYS_READ, fd, (long)buf, (long)len);
}

static unsigned long strlen(const char* s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

static void print(const char* s) {
    sys_write(1, s, (unsigned)strlen(s));
}

static void print_num(unsigned long v) {
    char buf[24];
    int i = 0;
    if (v == 0) buf[i++] = '0';
    while (v > 0 && i < 22) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    char out[24];
    int j = 0;
    while (i > 0) out[j++] = buf[--i];
    out[j++] = '\n';
    sys_write(1, out, (unsigned)j);
}

void user_main(void) {
    long pid = sys_getpid();

    print("user: hello from ring 3, pid ");
    print_num((unsigned long)pid);

    /* Busy work long enough to be timer-preempted (quantum 3 ticks). */
    for (unsigned long i = 0; i < 40000000UL; i++) {
        __asm__ volatile("nop");
    }
    print("user: survived preemption window\n");

    /* Round-robin with the sibling process. */
    for (int i = 1; i <= 3; i++) {
        print("user: yield ");
        print_num((unsigned long)i);
        sys_yield();
    }

    /* Userspace memory allocation via sbrk. */
    char* heap = (char*)sys_sbrk(4096);
    if (heap == (void*)-1) {
        print("user: sbrk FAILED\n");
        sys_exit(1);
    }
    for (int i = 0; i < 16; i++) heap[i] = (char)('A' + i);
    int ok = 1;
    for (int i = 0; i < 16; i++) {
        if (heap[i] != (char)('A' + i)) ok = 0;
    }
    print(ok ? "user: sbrk write/readback OK\n" : "user: sbrk verify FAILED\n");

    /* Phase 12: stdin is the terminal. Each process reads one canonical
     * line (echo + backspace editing already applied by the tty); kernel
     * boot self-test injected "hello\nworld\n" for the pair of processes. */
    char line[64];
    long got = sys_read(0, line, sizeof(line) - 1);
    if (got > 0) {
        line[got] = '\0';
        if (got > 1 && line[got - 1] == '\n') line[got - 1] = '\0';
        print("user: tty read: ");
        print(line);
        print("\n");
    } else {
        print("user: tty stdin EOF\n");
    }

    sys_exit(42);
}

__attribute__((section(".text.start")))
void _start(void) {
    user_main();
    sys_exit(1);
}
