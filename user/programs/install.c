/*
 * install.c - Native NumOS disk writer for simple install flows.
 *
 * This tool mirrors the host-side create_disk.py logic inside user space so
 * the OS can lay down its own FAT32 payload when running from a live image.
 */

#include "libc.h"
#include "syscalls.h"

/* Fixed image layout shared with the host-side disk builder. */

#define BYTES_PER_SECTOR    512u
#define SECTORS_PER_CLUSTER 8u
#define RESERVED_SECTORS    32u
#define NUM_FATS            2u
#define FAT_SIZE_SECTORS    160u
#define PARTITION_START_LBA 2048u
#define FS_TOTAL_SECTORS    61440u
#define PARTITION_TYPE_FAT32_LBA 0x0Cu
#define BYTES_PER_CLUSTER   (BYTES_PER_SECTOR * SECTORS_PER_CLUSTER)
#define DATA_START_SECTOR   (RESERVED_SECTORS + (NUM_FATS * FAT_SIZE_SECTORS))
#define TOTAL_CLUSTERS      ((FS_TOTAL_SECTORS - DATA_START_SECTOR) / SECTORS_PER_CLUSTER)

#define ROOT_CLUSTER    2u
#define INIT_CLUSTER    3u
#define BIN_CLUSTER     4u
#define RUN_CLUSTER     5u
#define HOME_CLUSTER    6u
#define INCLUDE_CLUSTER 7u
#define BOOT_CLUSTER    8u
#define BOOT_GRUB_CLUSTER 9u
#define FIRST_FILE_CLUSTER 10u

#define BOOT_CFG_PATH    "/boot/boot.cfg"
#define STATUS_CFG_PATH  "/boot/status.cfg"
#define DEFAULT_INIT_PATH "/bin/shell.elf"
#define DEFAULT_GFX_MODE "vesa"
#define MAX_KERNEL_VERSION 9999u
#define URL_PATH_BUF_SIZE 192u
#define HTTP_TIMEOUT_MS   5000u

#define MAX_STAGE_FILES 96
#define FILE_POOL_BYTES (8u * 1024u * 1024u)
#define ATA_MAX_TRANSFER_SECTORS 255u

#define FAT32_ATTR_ARCHIVE   0x20u
#define FAT32_EOC            0x0FFFFFFFu

/* Each staged file records the short FAT name and the payload location. */
struct staged_file {
    char name[13];
    char short_name[11];
    uint32_t size;
    uint32_t cluster;
    uint32_t clusters;
    const uint8_t *data;
};

struct remote_kernel_url {
    uint8_t remote_ip[4];
    uint16_t remote_port;
    uint8_t secure;
    char ip_text[16];
    char host[64];
    char path[URL_PATH_BUF_SIZE];
};

static struct staged_file bin_files[MAX_STAGE_FILES];
static struct staged_file run_files[MAX_STAGE_FILES];
static struct staged_file home_files[MAX_STAGE_FILES];
static struct staged_file include_files[MAX_STAGE_FILES];
static struct staged_file boot_files[MAX_STAGE_FILES];
static struct staged_file boot_grub_files[MAX_STAGE_FILES];

static int bin_count = 0;
static int run_count = 0;
static int home_count = 0;
static int include_count = 0;
static int boot_count = 0;
static int boot_grub_count = 0;

static uint8_t file_pool[FILE_POOL_BYTES];
static uint32_t file_pool_used = 0;

static uint8_t fat_buffer[FAT_SIZE_SECTORS * BYTES_PER_SECTOR];
static uint8_t cluster_buffer[BYTES_PER_CLUSTER];
static uint8_t zero_buffer[BYTES_PER_CLUSTER];
static uint8_t transfer_buffer[ATA_MAX_TRANSFER_SECTORS * BYTES_PER_SECTOR];

static int append_char(char *buf, size_t cap, size_t *pos, char c);
static int write_file_bytes(const char *path, const char *data, size_t len);
static int copy_file(const char *src_path, const char *dst_path);

/* Console helpers keep status output small and dependency free. */

static void write_str(const char *s) {
    sys_write(FD_STDOUT, s, strlen(s));
}

static void write_dec(uint64_t value) {
    char buf[32];
    char tmp[32];
    int pos = 0;
    int t = 0;

    if (value == 0) {
        sys_write(FD_STDOUT, "0", 1);
        return;
    }

    while (value > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (t > 0) {
        buf[pos++] = tmp[--t];
    }
    sys_write(FD_STDOUT, buf, (size_t)pos);
}

static void write_size_pretty(uint64_t bytes) {
    if (bytes >= 1024u * 1024u * 1024u) {
        uint64_t whole = bytes / (1024u * 1024u * 1024u);
        uint64_t frac = ((bytes % (1024u * 1024u * 1024u)) * 10u) /
                        (1024u * 1024u * 1024u);
        write_dec(whole);
        write_str(".");
        write_dec(frac);
        write_str(" GB");
        return;
    }

    write_dec(bytes / (1024u * 1024u));
    write_str(" MB");
}

static void store_u16_le(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void store_u32_le(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static char ascii_upper(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

static int ascii_casecmp(const char *a, const char *b) {
    if (!a || !b) return -1;

    while (*a && *b) {
        char ca = ascii_upper(*a++);
        char cb = ascii_upper(*b++);
        if (ca != cb) return (int)((unsigned char)ca - (unsigned char)cb);
    }

    return (int)((unsigned char)ascii_upper(*a) -
                 (unsigned char)ascii_upper(*b));
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int starts_with_text(const char *text, const char *prefix) {
    if (!text || !prefix) return 0;
    while (*prefix) {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_http_url(const char *text) {
    if (!text) return 0;
    return starts_with_text(text, "http://") || starts_with_text(text, "https://");
}

static int parse_ipv4(const char *text, uint8_t out[4]) {
    int part = 0;
    int value = 0;
    int have_digit = 0;

    if (!text || !out) return 0;

    while (*text) {
        if (is_digit(*text)) {
            value = (value * 10) + (*text - '0');
            if (value > 255) return 0;
            have_digit = 1;
        } else if (*text == '.') {
            if (!have_digit || part >= 3) return 0;
            out[part++] = (uint8_t)value;
            value = 0;
            have_digit = 0;
        } else {
            return 0;
        }
        text++;
    }

    if (!have_digit || part != 3) return 0;
    out[part] = (uint8_t)value;
    return 1;
}

static int parse_port_text(const char *text, uint16_t *out) {
    uint32_t value = 0;

    if (!text || !out || !*text) return 0;
    while (*text) {
        if (!is_digit(*text)) return 0;
        value = (value * 10u) + (uint32_t)(*text - '0');
        if (value > 65535u) return 0;
        text++;
    }
    if (value == 0) return 0;
    *out = (uint16_t)value;
    return 1;
}

static int is_default_remote_port(const struct remote_kernel_url *url) {
    if (!url) return 1;
    if (url->secure) return url->remote_port == 443u;
    return url->remote_port == 80u;
}

static int append_port_text(char *buf, size_t cap, size_t *pos, uint16_t port) {
    char tmp[8];
    int t = 0;

    if (!buf || !pos || port == 0) return -1;
    while (port > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (port % 10u));
        port /= 10u;
    }
    while (t > 0) {
        if (append_char(buf, cap, pos, tmp[--t]) != 0) return -1;
    }
    return 0;
}

static int parse_remote_kernel_url(const char *text, struct remote_kernel_url *out) {
    const char *scheme_end;
    const char *authority;
    const char *path;
    const char *colon = 0;
    size_t authority_len;
    size_t host_len;

    if (!text || !out) return -1;
    memset(out, 0, sizeof(*out));

    if (starts_with_text(text, "http://")) {
        out->secure = 0;
        out->remote_port = 80u;
        scheme_end = text + 7;
    } else if (starts_with_text(text, "https://")) {
        out->secure = 1;
        out->remote_port = 443u;
        scheme_end = text + 8;
    } else {
        return -1;
    }

    authority = scheme_end;
    path = scheme_end;
    while (*path && *path != '/' && *path != '?') path++;
    authority_len = (size_t)(path - authority);
    if (authority_len == 0) return -1;

    for (size_t i = 0; i < authority_len; i++) {
        char c = authority[i];
        if (c == ':') {
            if (colon) return -1;
            colon = authority + i;
            continue;
        }
        if (c != '.' && !is_digit(c)) return -1;
    }

    host_len = colon ? (size_t)(colon - authority) : authority_len;
    if (host_len == 0 || host_len >= sizeof(out->ip_text)) return -1;
    memcpy(out->ip_text, authority, host_len);
    out->ip_text[host_len] = '\0';
    if (!parse_ipv4(out->ip_text, out->remote_ip)) return -1;

    if (colon) {
        char port_text[8];
        size_t port_len = authority_len - host_len - 1;
        if (port_len == 0 || port_len >= sizeof(port_text)) return -1;
        memcpy(port_text, colon + 1, port_len);
        port_text[port_len] = '\0';
        if (!parse_port_text(port_text, &out->remote_port)) return -1;
    }

    if (*path == '\0') {
        strncpy(out->path, "/", sizeof(out->path) - 1);
    } else if (*path == '?') {
        out->path[0] = '/';
        strncpy(out->path + 1, path, sizeof(out->path) - 2);
    } else {
        strncpy(out->path, path, sizeof(out->path) - 1);
    }
    out->path[sizeof(out->path) - 1] = '\0';

    strncpy(out->host, out->ip_text, sizeof(out->host) - 1);
    out->host[sizeof(out->host) - 1] = '\0';
    if (!is_default_remote_port(out)) {
        size_t pos = strlen(out->host);
        if (append_char(out->host, sizeof(out->host), &pos, ':') != 0 ||
            append_port_text(out->host, sizeof(out->host), &pos, out->remote_port) != 0) {
            return -1;
        }
    }

    return 0;
}

static int ensure_network_ready(void) {
    struct numos_net_info info;

    if (sys_net_info(&info) != 0 || !info.present) {
        write_str("install: no supported NIC detected\n");
        return -1;
    }
    if (!info.dhcp_configured) {
        write_str("install: network is not configured\n");
        write_str("install: run connect --dhcp first\n");
        return -1;
    }
    return 0;
}

static int fetch_remote_kernel_bytes(const char *url_text,
                                     const uint8_t **out_data,
                                     size_t *out_len) {
    struct remote_kernel_url url;
    struct numos_net_http_request request;
    struct numos_net_http_result result;
    int64_t rc;

    if (!url_text || !out_data || !out_len) return -1;
    if (ensure_network_ready() != 0) return -1;
    if (parse_remote_kernel_url(url_text, &url) != 0) {
        write_str("install: unsupported kernel URL\n");
        return -1;
    }

    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    memcpy(request.remote_ip, url.remote_ip, sizeof(url.remote_ip));
    request.remote_port = url.remote_port;
    request.secure = url.secure;
    request.flags = url.secure ? NUMOS_NET_FLAG_INSECURE : 0u;
    request.timeout_ms = HTTP_TIMEOUT_MS;
    strncpy(request.host, url.host, sizeof(request.host) - 1);
    strncpy(request.path, url.path, sizeof(request.path) - 1);

    rc = sys_net_http_get(&request, file_pool, sizeof(file_pool), &result);
    if (rc < 0) {
        write_str("install: kernel download failed\n");
        return -1;
    }
    if (result.truncated || (size_t)rc > sizeof(file_pool)) {
        write_str("install: kernel download is too large for staging memory\n");
        return -1;
    }
    if (result.status_code < 200u || result.status_code >= 300u) {
        write_str("install: HTTP status ");
        write_dec(result.status_code);
        write_str("\n");
        return -1;
    }

    *out_data = file_pool;
    *out_len = (size_t)rc;
    return 0;
}

static int stage_kernel_from_source(const char *src_path, const char *dst_path) {
    if (!src_path || !dst_path) return -1;

    if (is_http_url(src_path)) {
        const uint8_t *downloaded = 0;
        size_t downloaded_len = 0;

        write_str("install: downloading kernel from ");
        write_str(src_path);
        write_str("\n");

        if (fetch_remote_kernel_bytes(src_path, &downloaded, &downloaded_len) != 0) {
            return -1;
        }
        if (downloaded_len == 0) {
            write_str("install: downloaded kernel is empty\n");
            return -1;
        }
        return write_file_bytes(dst_path, (const char *)downloaded, downloaded_len);
    }

    return copy_file(src_path, dst_path);
}

static int read_text_file(const char *path, char *buf, size_t cap) {
    int fd;
    size_t total = 0;

    if (!path || !buf || cap == 0) return -1;
    fd = (int)sys_open(path, FAT32_O_RDONLY, 0);
    if (fd < 0) return -1;

    while (total + 1 < cap) {
        int64_t got = sys_read(fd, buf + total, cap - total - 1);
        if (got < 0) {
            sys_close(fd);
            return -1;
        }
        if (got == 0) break;
        total += (size_t)got;
    }

    sys_close(fd);
    buf[total] = '\0';
    return 0;
}

static int write_file_bytes(const char *path, const char *data, size_t len) {
    int fd;
    size_t total = 0;

    if (!path || !data) return -1;
    fd = (int)sys_open(path, FAT32_O_WRONLY | FAT32_O_CREAT | FAT32_O_TRUNC, 0);
    if (fd < 0) return -1;

    while (total < len) {
        int64_t wrote = sys_write(fd, data + total, len - total);
        if (wrote <= 0) {
            sys_close(fd);
            return -1;
        }
        total += (size_t)wrote;
    }

    sys_close(fd);
    return 0;
}

static int copy_file(const char *src_path, const char *dst_path) {
    int src;
    int dst;
    uint8_t buf[512];

    if (!src_path || !dst_path) return -1;

    src = (int)sys_open(src_path, FAT32_O_RDONLY, 0);
    if (src < 0) return -1;

    dst = (int)sys_open(dst_path, FAT32_O_WRONLY | FAT32_O_CREAT | FAT32_O_TRUNC, 0);
    if (dst < 0) {
        sys_close(src);
        return -1;
    }

    for (;;) {
        int64_t got = sys_read(src, buf, sizeof(buf));
        if (got < 0) {
            sys_close(src);
            sys_close(dst);
            return -1;
        }
        if (got == 0) break;
        if (sys_write(dst, buf, (size_t)got) != got) {
            sys_close(src);
            sys_close(dst);
            return -1;
        }
    }

    sys_close(src);
    sys_close(dst);
    return 0;
}

static int append_char(char *buf, size_t cap, size_t *pos, char c) {
    if (!buf || !pos || *pos + 1 >= cap) return -1;
    buf[*pos] = c;
    (*pos)++;
    buf[*pos] = '\0';
    return 0;
}

static int append_text(char *buf, size_t cap, size_t *pos, const char *text) {
    if (!buf || !pos || !text) return -1;
    while (*text) {
        if (append_char(buf, cap, pos, *text++) != 0) return -1;
    }
    return 0;
}

static int append_uint_dec_fixed(char *buf, size_t cap, size_t *pos,
                                 unsigned value, unsigned digits) {
    char tmp[10];

    if (digits > sizeof(tmp)) return -1;
    for (unsigned i = 0; i < digits; i++) {
        tmp[digits - 1u - i] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    if (value != 0) return -1;
    for (unsigned i = 0; i < digits; i++) {
        if (append_char(buf, cap, pos, tmp[i]) != 0) return -1;
    }
    return 0;
}

static uint32_t clusters_for(uint32_t size) {
    if (size == 0) return 0;
    return (size + BYTES_PER_CLUSTER - 1u) / BYTES_PER_CLUSTER;
}

static int append_path(char *out, size_t cap, const char *dir, const char *name) {
    size_t pos = 0;
    if (!out || cap == 0) return -1;
    while (*dir && pos + 1 < cap) out[pos++] = *dir++;
    if (pos + 1 >= cap) return -1;
    if (pos > 0 && out[pos - 1] != '/') out[pos++] = '/';
    while (*name && pos + 1 < cap) out[pos++] = *name++;
    if (*name) return -1;
    out[pos] = '\0';
    return 0;
}

static int fat_format_name(const char *filename, char out[11]) {
    const char *dot = 0;
    int name_len = 0;
    int ext_len = 0;

    memset(out, ' ', 11);
    for (const char *p = filename; *p; p++) {
        if (*p == '.') dot = p;
    }

    if (dot) {
        name_len = (int)(dot - filename);
        ext_len = (int)strlen(dot + 1);
    } else {
        name_len = (int)strlen(filename);
        ext_len = 0;
    }

    if (name_len <= 0 || name_len > 8 || ext_len > 3) return -1;

    for (int i = 0; i < name_len; i++) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[i] = c;
    }

    for (int i = 0; i < ext_len; i++) {
        char c = dot[1 + i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[8 + i] = c;
    }

    return 0;
}

static int load_file_data(const char *path, uint32_t size, const uint8_t **out_data) {
    int fd;
    uint32_t total = 0;
    uint8_t *dst;

    if (!out_data) return -1;
    if (file_pool_used + size > FILE_POOL_BYTES) return -1;

    dst = file_pool + file_pool_used;
    fd = (int)sys_open(path, FAT32_O_RDONLY, 0);
    if (fd < 0) return -1;

    while (total < size) {
        int64_t got = sys_read(fd, dst + total, size - total);
        if (got <= 0) {
            sys_close(fd);
            return -1;
        }
        total += (uint32_t)got;
    }

    sys_close(fd);
    *out_data = dst;
    file_pool_used += size;
    return 0;
}

static int stage_named_file(const char *dir, const char *name, uint32_t size,
                            struct staged_file *files, int *count) {
    char path[128];
    char short_name[11];
    struct staged_file *f;

    if (!files || !count || *count >= MAX_STAGE_FILES) return -1;
    if (fat_format_name(name, short_name) != 0) return 0;
    if (append_path(path, sizeof(path), dir, name) != 0) return -1;

    f = &files[*count];
    memset(f, 0, sizeof(*f));
    strncpy(f->name, name, sizeof(f->name) - 1);
    memcpy(f->short_name, short_name, 11);
    f->size = size;
    f->clusters = clusters_for(size);

    if (load_file_data(path, size, &f->data) != 0) return -1;

    (*count)++;
    return 0;
}

static int stage_directory(const char *dir, struct staged_file *files, int *count) {
    struct fat32_dirent entries[64];
    int64_t n = sys_listdir(dir, entries, 64);
    if (n < 0) return -1;

    for (int i = 0; i < n; i++) {
        if (entries[i].attr & FAT32_ATTR_DIRECTORY) continue;
        if (stage_named_file(dir, entries[i].name, entries[i].size, files, count) != 0) {
            return -1;
        }
    }
    return 0;
}

static int find_staged_file(const struct staged_file *files, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (ascii_casecmp(files[i].name, name) == 0) return i;
    }
    return -1;
}

static int parse_boot_cfg_value(const char *text, const char *key, char *out, size_t cap) {
    size_t key_len;
    const char *cursor;

    if (!text || !key || !out || cap == 0) return -1;
    key_len = strlen(key);
    cursor = text;

    while (*cursor) {
        while (*cursor && is_space(*cursor)) cursor++;
        if (!*cursor) break;

        if (starts_with_text(cursor, "set ")) {
            const char *line = cursor + 4;
            while (*line == ' ' || *line == '\t') line++;
            if (strncmp(line, key, key_len) == 0 && line[key_len] == '=') {
                const char *value = line + key_len + 1;
                size_t pos = 0;
                char quote = 0;

                while (*value == ' ' || *value == '\t') value++;
                if (*value == '"' || *value == '\'') quote = *value++;

                while (*value) {
                    if (quote) {
                        if (*value == quote) break;
                    } else if (*value == '\r' || *value == '\n') {
                        break;
                    }

                    if (pos + 1 < cap) out[pos++] = *value;
                    value++;
                }

                out[pos] = '\0';
                return 0;
            }
        }

        while (*cursor && *cursor != '\n') cursor++;
        if (*cursor == '\n') cursor++;
    }

    return -1;
}

static int write_boot_cfg(const char *default_kernel, const char *fallback_kernel,
                          const char *default_gfx, const char *fallback_gfx) {
    char text[512];
    size_t pos = 0;

    if (!default_kernel || !fallback_kernel || !default_gfx || !fallback_gfx) return -1;
    text[0] = '\0';

    if (append_text(text, sizeof(text), &pos, "set numos_default_kernel=\"") != 0 ||
        append_text(text, sizeof(text), &pos, default_kernel) != 0 ||
        append_text(text, sizeof(text), &pos, "\"\nset numos_fallback_kernel=\"") != 0 ||
        append_text(text, sizeof(text), &pos, fallback_kernel) != 0 ||
        append_text(text, sizeof(text), &pos, "\"\nset numos_default_gfx=\"") != 0 ||
        append_text(text, sizeof(text), &pos, default_gfx) != 0 ||
        append_text(text, sizeof(text), &pos, "\"\nset numos_fallback_gfx=\"") != 0 ||
        append_text(text, sizeof(text), &pos, fallback_gfx) != 0 ||
        append_text(text, sizeof(text), &pos, "\"\nset numos_init_path=\"") != 0 ||
        append_text(text, sizeof(text), &pos, DEFAULT_INIT_PATH) != 0 ||
        append_text(text, sizeof(text), &pos, "\"\n") != 0) {
        return -1;
    }

    return write_file_bytes(BOOT_CFG_PATH, text, pos);
}

static int write_boot_status(const char *status) {
    char text[64];
    size_t pos = 0;

    if (!status) return -1;
    text[0] = '\0';
    if (append_text(text, sizeof(text), &pos, "set numos_boot_status=\"") != 0 ||
        append_text(text, sizeof(text), &pos, status) != 0 ||
        append_text(text, sizeof(text), &pos, "\"\n") != 0) {
        return -1;
    }
    return write_file_bytes(STATUS_CFG_PATH, text, pos);
}

static int parse_kernel_version(const char *name) {
    unsigned value = 0;

    if (!name) return -1;
    if (ascii_upper(name[0]) != 'K' ||
        ascii_upper(name[1]) != 'E' ||
        ascii_upper(name[2]) != 'R' ||
        ascii_upper(name[3]) != 'N') {
        return -1;
    }

    for (int i = 4; i < 8; i++) {
        char c = name[i];
        if (c < '0' || c > '9') return -1;
        value = (value * 10u) + (unsigned)(c - '0');
    }

    if (name[8] != '.' ||
        ascii_upper(name[9]) != 'B' ||
        ascii_upper(name[10]) != 'I' ||
        ascii_upper(name[11]) != 'N' ||
        name[12] != '\0') {
        return -1;
    }

    return (int)value;
}

static int build_kernel_path(unsigned version, char *out, size_t cap) {
    size_t pos = 0;

    if (!out || cap == 0 || version > MAX_KERNEL_VERSION) return -1;
    out[0] = '\0';
    if (append_text(out, cap, &pos, "/boot/kern") != 0 ||
        append_uint_dec_fixed(out, cap, &pos, version, 4) != 0 ||
        append_text(out, cap, &pos, ".bin") != 0) {
        return -1;
    }
    return 0;
}

static int find_highest_kernel_version(void) {
    struct fat32_dirent entries[64];
    int highest = 0;
    int64_t count = sys_listdir("/boot", entries, 64);

    if (count < 0) return -1;

    for (int i = 0; i < count; i++) {
        int version;
        if (entries[i].attr & FAT32_ATTR_DIRECTORY) continue;
        version = parse_kernel_version(entries[i].name);
        if (version > highest) highest = version;
    }

    return highest;
}

static uint32_t sectors_for_bytes(uint32_t size) {
    if (size == 0) return 0;
    return (size + BYTES_PER_SECTOR - 1u) / BYTES_PER_SECTOR;
}

static void assign_clusters(struct staged_file *files, int count, uint32_t *next_cluster) {
    for (int i = 0; i < count; i++) {
        if (files[i].clusters == 0) continue;
        files[i].cluster = *next_cluster;
        *next_cluster += files[i].clusters;
    }
}

static void set_fat_entry(uint32_t cluster, uint32_t value) {
    store_u32_le(&fat_buffer[cluster * 4u], value);
}

static void build_fat(void) {
    memset(fat_buffer, 0, sizeof(fat_buffer));

    set_fat_entry(0, 0x0FFFFFF8u);
    set_fat_entry(1, 0x0FFFFFFFu);
    set_fat_entry(ROOT_CLUSTER, FAT32_EOC);
    set_fat_entry(INIT_CLUSTER, FAT32_EOC);
    set_fat_entry(BIN_CLUSTER, FAT32_EOC);
    set_fat_entry(RUN_CLUSTER, FAT32_EOC);
    set_fat_entry(HOME_CLUSTER, FAT32_EOC);
    set_fat_entry(INCLUDE_CLUSTER, FAT32_EOC);
    set_fat_entry(BOOT_CLUSTER, FAT32_EOC);
    set_fat_entry(BOOT_GRUB_CLUSTER, FAT32_EOC);

    for (int group = 0; group < 6; group++) {
        struct staged_file *files = 0;
        int count = 0;

        if (group == 0) { files = include_files; count = include_count; }
        if (group == 1) { files = bin_files; count = bin_count; }
        if (group == 2) { files = run_files; count = run_count; }
        if (group == 3) { files = home_files; count = home_count; }
        if (group == 4) { files = boot_files; count = boot_count; }
        if (group == 5) { files = boot_grub_files; count = boot_grub_count; }

        for (int i = 0; i < count; i++) {
            if (files[i].clusters == 0) continue;
            for (uint32_t c = 0; c < files[i].clusters; c++) {
                uint32_t cluster = files[i].cluster + c;
                uint32_t value = (c + 1u == files[i].clusters) ? FAT32_EOC : (cluster + 1u);
                set_fat_entry(cluster, value);
            }
        }
    }
}

static void create_directory_entry(uint8_t *entry, const char short_name[11],
                                   uint8_t attr, uint32_t cluster, uint32_t size) {
    memset(entry, 0, 32);
    memcpy(entry + 0, short_name, 11);
    entry[11] = attr;
    store_u16_le(entry + 20, (uint16_t)((cluster >> 16) & 0xFFFFu));
    store_u16_le(entry + 26, (uint16_t)(cluster & 0xFFFFu));
    store_u32_le(entry + 28, size);
}

static void build_root_directory(void) {
    static const char init_name[11] = { 'I','N','I','T',' ',' ',' ',' ',' ',' ',' ' };
    static const char bin_name[11] = { 'B','I','N',' ',' ',' ',' ',' ',' ',' ',' ' };
    static const char run_name[11] = { 'R','U','N',' ',' ',' ',' ',' ',' ',' ',' ' };
    static const char home_name[11] = { 'H','O','M','E',' ',' ',' ',' ',' ',' ',' ' };
    static const char incl_name[11] = { 'I','N','C','L','U','D','E',' ',' ',' ',' ' };
    static const char boot_name[11] = { 'B','O','O','T',' ',' ',' ',' ',' ',' ',' ' };

    memset(cluster_buffer, 0, sizeof(cluster_buffer));
    create_directory_entry(cluster_buffer + 0, init_name, FAT32_ATTR_DIRECTORY, INIT_CLUSTER, 0);
    create_directory_entry(cluster_buffer + 32, bin_name, FAT32_ATTR_DIRECTORY, BIN_CLUSTER, 0);
    create_directory_entry(cluster_buffer + 64, run_name, FAT32_ATTR_DIRECTORY, RUN_CLUSTER, 0);
    create_directory_entry(cluster_buffer + 96, home_name, FAT32_ATTR_DIRECTORY, HOME_CLUSTER, 0);
    create_directory_entry(cluster_buffer + 128, incl_name, FAT32_ATTR_DIRECTORY, INCLUDE_CLUSTER, 0);
    create_directory_entry(cluster_buffer + 160, boot_name, FAT32_ATTR_DIRECTORY, BOOT_CLUSTER, 0);
}

static int build_child_directory(uint32_t self_cluster, uint32_t parent_cluster,
                                 struct staged_file *files, int count) {
    static const char dot_name[11] = { '.',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ' };
    static const char dotdot_name[11] = { '.','.',' ',' ',' ',' ',' ',' ',' ',' ',' ' };

    if (count > ((int)BYTES_PER_CLUSTER / 32) - 2) return -1;

    memset(cluster_buffer, 0, sizeof(cluster_buffer));
    create_directory_entry(cluster_buffer + 0, dot_name, FAT32_ATTR_DIRECTORY, self_cluster, 0);
    create_directory_entry(cluster_buffer + 32, dotdot_name, FAT32_ATTR_DIRECTORY, parent_cluster, 0);

    for (int i = 0; i < count; i++) {
        create_directory_entry(cluster_buffer + ((i + 2) * 32),
                               files[i].short_name,
                               FAT32_ATTR_ARCHIVE,
                               files[i].cluster,
                               files[i].size);
    }
    return 0;
}

static int build_boot_directory(void) {
    static const char dot_name[11] = { '.',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ' };
    static const char dotdot_name[11] = { '.','.',' ',' ',' ',' ',' ',' ',' ',' ',' ' };
    static const char grub_name[11] = { 'G','R','U','B',' ',' ',' ',' ',' ',' ',' ' };

    if (boot_count > ((int)BYTES_PER_CLUSTER / 32) - 3) return -1;

    memset(cluster_buffer, 0, sizeof(cluster_buffer));
    create_directory_entry(cluster_buffer + 0, dot_name, FAT32_ATTR_DIRECTORY, BOOT_CLUSTER, 0);
    create_directory_entry(cluster_buffer + 32, dotdot_name, FAT32_ATTR_DIRECTORY, ROOT_CLUSTER, 0);
    create_directory_entry(cluster_buffer + 64, grub_name, FAT32_ATTR_DIRECTORY, BOOT_GRUB_CLUSTER, 0);

    for (int i = 0; i < boot_count; i++) {
        create_directory_entry(cluster_buffer + ((i + 3) * 32),
                               boot_files[i].short_name,
                               FAT32_ATTR_ARCHIVE,
                               boot_files[i].cluster,
                               boot_files[i].size);
    }
    return 0;
}

static void create_boot_sector(uint8_t *boot) {
    memset(boot, 0, BYTES_PER_SECTOR);
    boot[0] = 0xEBu;
    boot[1] = 0x58u;
    boot[2] = 0x90u;
    memcpy(boot + 3, "NUMOS1.0", 8);
    store_u16_le(boot + 11, BYTES_PER_SECTOR);
    boot[13] = SECTORS_PER_CLUSTER;
    store_u16_le(boot + 14, RESERVED_SECTORS);
    boot[16] = NUM_FATS;
    store_u16_le(boot + 17, 0);
    store_u16_le(boot + 19, 0);
    boot[21] = 0xF8u;
    store_u16_le(boot + 22, 0);
    store_u16_le(boot + 24, 63);
    store_u16_le(boot + 26, 16);
    store_u32_le(boot + 28, PARTITION_START_LBA);
    store_u32_le(boot + 32, FS_TOTAL_SECTORS);
    store_u32_le(boot + 36, FAT_SIZE_SECTORS);
    store_u16_le(boot + 40, 0);
    store_u16_le(boot + 42, 0);
    store_u32_le(boot + 44, ROOT_CLUSTER);
    store_u16_le(boot + 48, 1);
    store_u16_le(boot + 50, 6);
    boot[64] = 0x80u;
    boot[66] = 0x29u;
    store_u32_le(boot + 67, 0x12345678u);
    memcpy(boot + 71, "NUMOS DISK ", 11);
    memcpy(boot + 82, "FAT32   ", 8);
    boot[510] = 0x55u;
    boot[511] = 0xAAu;
}

static void create_fsinfo(uint8_t *fsinfo, uint32_t free_clusters, uint32_t next_free_cluster) {
    memset(fsinfo, 0, BYTES_PER_SECTOR);
    store_u32_le(fsinfo + 0, 0x41615252u);
    store_u32_le(fsinfo + 484, 0x61417272u);
    store_u32_le(fsinfo + 488, free_clusters);
    store_u32_le(fsinfo + 492, next_free_cluster);
    store_u32_le(fsinfo + 508, 0xAA550000u);
}

static int disk_write_repeat(uint64_t lba, const uint8_t *buf, uint32_t total_sectors) {
    uint64_t current = lba;
    uint32_t remaining = total_sectors;
    (void)buf;

    while (remaining > 0) {
        uint32_t chunk = remaining > ATA_MAX_TRANSFER_SECTORS
                       ? ATA_MAX_TRANSFER_SECTORS
                       : remaining;
        if (sys_disk_write(current, transfer_buffer, chunk) < 0) return -1;
        current += chunk;
        remaining -= chunk;
    }
    return 0;
}

static int write_cluster(uint64_t partition_lba, uint32_t cluster, const uint8_t *data) {
    uint64_t lba = partition_lba + DATA_START_SECTOR +
                   ((uint64_t)(cluster - 2u) * SECTORS_PER_CLUSTER);
    return sys_disk_write(lba, data, SECTORS_PER_CLUSTER) < 0 ? -1 : 0;
}

static int write_padded_payload(uint64_t lba, const uint8_t *data,
                                uint32_t size, uint32_t padded_size) {
    uint32_t written = 0;

    while (written < padded_size) {
        uint32_t chunk_bytes = padded_size - written;
        uint32_t chunk_sectors;
        uint32_t copy = 0;

        if (chunk_bytes > ATA_MAX_TRANSFER_SECTORS * BYTES_PER_SECTOR) {
            chunk_bytes = ATA_MAX_TRANSFER_SECTORS * BYTES_PER_SECTOR;
        }
        chunk_sectors = chunk_bytes / BYTES_PER_SECTOR;

        if (written < size) {
            copy = size - written;
            if (copy > chunk_bytes) copy = chunk_bytes;
        }

        if (copy == chunk_bytes && (chunk_bytes % BYTES_PER_SECTOR) == 0) {
            if (sys_disk_write(lba, data + written, chunk_sectors) < 0) return -1;
        } else {
            memset(transfer_buffer, 0, chunk_bytes);
            if (copy > 0) memcpy(transfer_buffer, data + written, copy);
            if (sys_disk_write(lba, transfer_buffer, chunk_sectors) < 0) return -1;
        }

        lba += chunk_sectors;
        written += chunk_bytes;
    }

    return 0;
}

static int write_file_sectors(uint64_t lba, const struct staged_file *file) {
    uint32_t written = 0;
    uint64_t current_lba = lba;

    if (!file) return -1;
    if (file->size == 0) return 0;

    while (written < file->size) {
        uint32_t copy = file->size - written;
        uint32_t sectors;
        uint32_t bytes;

        if (copy > sizeof(cluster_buffer)) copy = sizeof(cluster_buffer);
        sectors = sectors_for_bytes(copy);
        bytes = sectors * BYTES_PER_SECTOR;

        memset(cluster_buffer, 0, bytes);
        memcpy(cluster_buffer, file->data + written, copy);
        if (sys_disk_write(current_lba, cluster_buffer, sectors) < 0) return -1;

        current_lba += sectors;
        written += copy;
    }

    return 0;
}

static int write_all_files(uint64_t partition_lba,
                           struct staged_file *files, int count) {
    for (int i = 0; i < count; i++) {
        uint64_t lba = partition_lba + DATA_START_SECTOR +
                       ((uint64_t)(files[i].cluster - 2u) * SECTORS_PER_CLUSTER);
        uint32_t padded_size = files[i].clusters * BYTES_PER_CLUSTER;

        write_str("install: writing ");
        write_str(files[i].name);
        write_str("\n");

        if (files[i].clusters == 0) continue;
        if (write_padded_payload(lba, files[i].data, files[i].size, padded_size) != 0) {
            return -1;
        }
    }
    return 0;
}

static void create_mbr(uint8_t *sector, const struct staged_file *grub_boot) {
    memset(sector, 0, BYTES_PER_SECTOR);
    if (grub_boot && grub_boot->size >= BYTES_PER_SECTOR) {
        memcpy(sector, grub_boot->data, 446);
    }
    sector[446 + 0] = 0x00u;
    sector[446 + 1] = 0xFEu;
    sector[446 + 2] = 0xFFu;
    sector[446 + 3] = 0xFFu;
    sector[446 + 4] = PARTITION_TYPE_FAT32_LBA;
    sector[446 + 5] = 0xFEu;
    sector[446 + 6] = 0xFFu;
    sector[446 + 7] = 0xFFu;
    store_u32_le(sector + 446 + 8, PARTITION_START_LBA);
    store_u32_le(sector + 446 + 12, FS_TOTAL_SECTORS);
    sector[510] = 0x55u;
    sector[511] = 0xAAu;
}

static int install_kernel_image(const char *src_path, int reboot_after) {
    struct fat32_dirent entries[64];
    char boot_cfg[512];
    char current_default[64];
    char current_fallback[64];
    char default_gfx[16];
    char fallback_gfx[16];
    char new_kernel_path[64];
    char fallback_kernel[64];
    int highest_version;
    int next_version;

    if (!src_path || src_path[0] == '\0') {
        write_str("install: missing kernel path\n");
        return 1;
    }

    if (sys_listdir("/boot", entries, 64) < 0) {
        write_str("install: missing /boot directory\n");
        return 1;
    }
    if (sys_listdir("/boot/grub", entries, 64) < 0) {
        write_str("install: missing /boot/grub directory\n");
        return 1;
    }

    highest_version = find_highest_kernel_version();
    if (highest_version < 0) {
        write_str("install: failed to scan /boot\n");
        return 1;
    }

    next_version = highest_version + 1;
    if (next_version <= 0 || next_version > (int)MAX_KERNEL_VERSION) {
        write_str("install: kernel version space exhausted\n");
        return 1;
    }
    if (build_kernel_path((unsigned)next_version, new_kernel_path, sizeof(new_kernel_path)) != 0) {
        write_str("install: failed to build kernel path\n");
        return 1;
    }

    strncpy(current_default, "/boot/kern0001.bin", sizeof(current_default) - 1);
    current_default[sizeof(current_default) - 1] = '\0';
    strncpy(current_fallback, current_default, sizeof(current_fallback) - 1);
    current_fallback[sizeof(current_fallback) - 1] = '\0';
    strncpy(default_gfx, DEFAULT_GFX_MODE, sizeof(default_gfx) - 1);
    default_gfx[sizeof(default_gfx) - 1] = '\0';
    strncpy(fallback_gfx, DEFAULT_GFX_MODE, sizeof(fallback_gfx) - 1);
    fallback_gfx[sizeof(fallback_gfx) - 1] = '\0';

    if (read_text_file(BOOT_CFG_PATH, boot_cfg, sizeof(boot_cfg)) == 0) {
        parse_boot_cfg_value(boot_cfg, "numos_default_kernel",
                             current_default, sizeof(current_default));
        parse_boot_cfg_value(boot_cfg, "numos_fallback_kernel",
                             current_fallback, sizeof(current_fallback));
        parse_boot_cfg_value(boot_cfg, "numos_default_gfx",
                             default_gfx, sizeof(default_gfx));
        parse_boot_cfg_value(boot_cfg, "numos_fallback_gfx",
                             fallback_gfx, sizeof(fallback_gfx));
    }

    strncpy(fallback_kernel, current_default, sizeof(fallback_kernel) - 1);
    fallback_kernel[sizeof(fallback_kernel) - 1] = '\0';
    if (fallback_kernel[0] == '\0') {
        strncpy(fallback_kernel, current_fallback, sizeof(fallback_kernel) - 1);
        fallback_kernel[sizeof(fallback_kernel) - 1] = '\0';
    }
    if (fallback_kernel[0] == '\0') {
        strncpy(fallback_kernel, new_kernel_path, sizeof(fallback_kernel) - 1);
        fallback_kernel[sizeof(fallback_kernel) - 1] = '\0';
    }

    write_str("install: staging kernel ");
    write_str(src_path);
    write_str(" -> ");
    write_str(new_kernel_path);
    write_str("\n");

    if (stage_kernel_from_source(src_path, new_kernel_path) != 0) {
        write_str("install: failed to stage new kernel into /boot\n");
        return 1;
    }

    if (write_boot_cfg(new_kernel_path, fallback_kernel, default_gfx, fallback_gfx) != 0) {
        write_str("install: failed to update /boot/boot.cfg\n");
        return 1;
    }
    if (write_boot_status("pending") != 0) {
        write_str("install: failed to update /boot/status.cfg\n");
        return 1;
    }

    write_str("install: kernel staged\n");
    write_str("install: pending boot marked, old kernel kept as fallback\n");

    if (reboot_after) {
        write_str("install: rebooting\n");
        sys_reboot();
    } else {
        write_str("install: reboot to activate the new kernel\n");
    }

    return 0;
}

static int install_to_primary_disk(void) {
    struct numos_disk_info info;
    uint8_t sector[BYTES_PER_SECTOR];
    uint32_t next_cluster = FIRST_FILE_CLUSTER;
    uint64_t partition_lba = PARTITION_START_LBA;
    struct fat32_dirent include_entries[16];
    int64_t include_dir_count = 0;
    int boot_idx = -1;
    int core_idx = -1;
    uint32_t grub_core_sectors = 0;

    if (sys_disk_info(&info) < 0 || !info.present) {
        write_str("install: no primary ATA disk\n");
        return 1;
    }

    if (info.sector_count < (uint64_t)PARTITION_START_LBA + FS_TOTAL_SECTORS) {
        write_str("install: disk is too small, need at least ");
        write_size_pretty(((uint64_t)PARTITION_START_LBA + FS_TOTAL_SECTORS) *
                          BYTES_PER_SECTOR);
        write_str("\n");
        return 1;
    }

    write_str("install: target disk ");
    write_size_pretty(info.sector_count * (uint64_t)info.sector_size);
    write_str("\n");

    include_dir_count = sys_listdir("/include", include_entries, 16);
    if (include_dir_count < 0) {
        write_str("install: failed to scan /include\n");
        return 1;
    }
    for (int i = 0; i < include_dir_count; i++) {
        if (ascii_casecmp(include_entries[i].name, "SYSCALLS.H") != 0) continue;
        if (stage_named_file("/include", include_entries[i].name, include_entries[i].size,
                             include_files, &include_count) != 0) {
            write_str("install: failed to stage /include/SYSCALLS.H\n");
            return 1;
        }
        break;
    }

    if (include_count == 0) {
        write_str("install: missing /include/SYSCALLS.H\n");
        return 1;
    }

    if (stage_directory("/bin", bin_files, &bin_count) != 0 ||
        stage_directory("/run", run_files, &run_count) != 0 ||
        stage_directory("/home", home_files, &home_count) != 0 ||
        stage_directory("/boot", boot_files, &boot_count) != 0 ||
        stage_directory("/boot/grub", boot_grub_files, &boot_grub_count) != 0) {
        write_str("install: failed to stage files from the current system\n");
        return 1;
    }

    if (find_staged_file(bin_files, bin_count, "SHELL.ELF") < 0) {
        write_str("install: missing /bin/SHELL.ELF\n");
        return 1;
    }

    boot_idx = find_staged_file(run_files, run_count, "GRUBBOOT.BIN");
    if (boot_idx < 0) {
        write_str("install: missing /run/GRUBBOOT.BIN\n");
        return 1;
    }

    core_idx = find_staged_file(run_files, run_count, "GRUBCORE.BIN");
    if (core_idx < 0) {
        write_str("install: missing /run/GRUBCORE.BIN\n");
        return 1;
    }

    if (find_staged_file(boot_files, boot_count, "BOOT.CFG") < 0 &&
        find_staged_file(boot_files, boot_count, "boot.cfg") < 0) {
        write_str("install: missing /boot/boot.cfg\n");
        return 1;
    }

    if (find_staged_file(boot_files, boot_count, "STATUS.CFG") < 0 &&
        find_staged_file(boot_files, boot_count, "status.cfg") < 0) {
        write_str("install: missing /boot/status.cfg\n");
        return 1;
    }

    if (find_staged_file(boot_grub_files, boot_grub_count, "GRUBENV") < 0 &&
        find_staged_file(boot_grub_files, boot_grub_count, "grubenv") < 0) {
        write_str("install: missing /boot/grub/grubenv\n");
        return 1;
    }

    grub_core_sectors = sectors_for_bytes(run_files[core_idx].size);
    if (grub_core_sectors >= PARTITION_START_LBA) {
        write_str("install: GRUB core image does not fit before partition 1\n");
        return 1;
    }

    assign_clusters(include_files, include_count, &next_cluster);
    assign_clusters(bin_files, bin_count, &next_cluster);
    assign_clusters(run_files, run_count, &next_cluster);
    assign_clusters(home_files, home_count, &next_cluster);
    assign_clusters(boot_files, boot_count, &next_cluster);
    assign_clusters(boot_grub_files, boot_grub_count, &next_cluster);

    write_str("install: staged files into memory, writing disk\n");

    write_str("install: writing MBR and GRUB core\n");
    create_mbr(sector, &run_files[boot_idx]);
    if (sys_disk_write(0, sector, 1) < 0) {
        write_str("install: failed to write MBR\n");
        return 1;
    }

    if (write_file_sectors(1, &run_files[core_idx]) != 0) {
        write_str("install: failed to write embedded GRUB core\n");
        return 1;
    }

    if (PARTITION_START_LBA - 1u > grub_core_sectors &&
        disk_write_repeat(1u + grub_core_sectors, zero_buffer,
                          (PARTITION_START_LBA - 1u) - grub_core_sectors) != 0) {
        write_str("install: failed to clear disk gap\n");
        return 1;
    }

    write_str("install: writing FAT32 metadata\n");
    create_boot_sector(sector);
    if (sys_disk_write(partition_lba + 0, sector, 1) < 0) {
        write_str("install: failed to write boot sector\n");
        return 1;
    }

    create_fsinfo(sector,
                  TOTAL_CLUSTERS - (next_cluster - ROOT_CLUSTER),
                  next_cluster);
    if (sys_disk_write(partition_lba + 1, sector, 1) < 0) {
        write_str("install: failed to write fsinfo\n");
        return 1;
    }

    memset(sector, 0, sizeof(sector));
    for (uint32_t i = 2; i < RESERVED_SECTORS; i++) {
        if (i == 6u) {
            create_boot_sector(sector);
        } else {
            memset(sector, 0, sizeof(sector));
        }
        if (sys_disk_write(partition_lba + i, sector, 1) < 0) {
            write_str("install: failed during reserved sector write\n");
            return 1;
        }
    }

    build_fat();
    if (sys_disk_write(partition_lba + RESERVED_SECTORS, fat_buffer, FAT_SIZE_SECTORS) < 0 ||
        sys_disk_write(partition_lba + RESERVED_SECTORS + FAT_SIZE_SECTORS,
                       fat_buffer, FAT_SIZE_SECTORS) < 0) {
        write_str("install: failed to write FAT tables\n");
        return 1;
    }

    write_str("install: writing directories\n");
    build_root_directory();
    if (write_cluster(partition_lba, ROOT_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write root directory\n");
        return 1;
    }

    memset(cluster_buffer, 0, sizeof(cluster_buffer));
    if (write_cluster(partition_lba, INIT_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write /init\n");
        return 1;
    }

    if (build_child_directory(BIN_CLUSTER, ROOT_CLUSTER, bin_files, bin_count) != 0 ||
        write_cluster(partition_lba, BIN_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write /bin\n");
        return 1;
    }

    if (build_child_directory(RUN_CLUSTER, ROOT_CLUSTER, run_files, run_count) != 0 ||
        write_cluster(partition_lba, RUN_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write /run\n");
        return 1;
    }

    if (build_child_directory(HOME_CLUSTER, ROOT_CLUSTER, home_files, home_count) != 0 ||
        write_cluster(partition_lba, HOME_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write /home\n");
        return 1;
    }

    if (build_child_directory(INCLUDE_CLUSTER, ROOT_CLUSTER, include_files, include_count) != 0 ||
        write_cluster(partition_lba, INCLUDE_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write /include\n");
        return 1;
    }

    if (build_boot_directory() != 0 ||
        write_cluster(partition_lba, BOOT_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write /boot\n");
        return 1;
    }

    if (build_child_directory(BOOT_GRUB_CLUSTER, BOOT_CLUSTER,
                              boot_grub_files, boot_grub_count) != 0 ||
        write_cluster(partition_lba, BOOT_GRUB_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write /boot/grub\n");
        return 1;
    }

    write_str("install: writing file payloads\n");
    if (write_all_files(partition_lba, include_files, include_count) != 0 ||
        write_all_files(partition_lba, bin_files, bin_count) != 0 ||
        write_all_files(partition_lba, run_files, run_count) != 0 ||
        write_all_files(partition_lba, home_files, home_count) != 0 ||
        write_all_files(partition_lba, boot_files, boot_count) != 0 ||
        write_all_files(partition_lba, boot_grub_files, boot_grub_count) != 0) {
        write_str("install: failed to write file data\n");
        return 1;
    }

    if (next_cluster > ROOT_CLUSTER + ((FS_TOTAL_SECTORS - DATA_START_SECTOR) / SECTORS_PER_CLUSTER)) {
        write_str("install: filesystem layout overflow\n");
        return 1;
    }

    {
        uint64_t data_end_lba = partition_lba + DATA_START_SECTOR +
                                ((uint64_t)(next_cluster - 2u) * SECTORS_PER_CLUSTER);
        uint64_t partition_end_lba = partition_lba + FS_TOTAL_SECTORS;
        write_str("install: clearing remaining free space\n");
        while (data_end_lba < partition_end_lba) {
            uint32_t chunk = (partition_end_lba - data_end_lba > ATA_MAX_TRANSFER_SECTORS)
                           ? ATA_MAX_TRANSFER_SECTORS
                           : (uint32_t)(partition_end_lba - data_end_lba);
            if (sys_disk_write(data_end_lba, transfer_buffer, chunk) < 0) {
                write_str("install: failed to clear trailing sectors\n");
                return 1;
            }
            data_end_lba += chunk;
        }
    }

    write_str("install: done\n");
    write_str("install: reboot from the hard disk\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "kernel") == 0) {
        int reboot_after = 0;
        if (argc >= 4 &&
            (strcmp(argv[3], "reboot") == 0 || strcmp(argv[3], "now") == 0)) {
            reboot_after = 1;
        }
        return install_kernel_image(argv[2], reboot_after);
    }

    if (argc >= 2 && (strcmp(argv[1], "ata") == 0 || strcmp(argv[1], "disk") == 0)) {
        return install_to_primary_disk();
    }

    {
        write_str("usage: install ata\n");
        write_str("       install kernel <path|URL>\n");
        write_str("       install kernel <path|URL> reboot\n");
        write_str("writes a bootable 30 MB NumOS system partition to the primary ATA disk\n");
        write_str("or stages a new immutable kernel in /boot and marks the next boot pending\n");
    }
    return 1;
}
