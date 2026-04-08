#include "pkg_kernel.h"

#include "ctype.h"
#include "libc.h"
#include "string.h"
#include "syscalls.h"

#define BOOT_CFG_PATH "/boot/boot.cfg"
#define STATUS_CFG_PATH "/boot/status.cfg"
#define DEFAULT_INIT_PATH "/bin/shell.elf"
#define DEFAULT_GFX_MODE "vesa"
#define MAX_KERNEL_VERSION 9999u
#define MULTIBOOT2_HEADER_MAGIC 0xE85250D6u
#define MULTIBOOT2_TAG_END 0u
#define MULTIBOOT2_TAG_FRAMEBUFFER 5u
#define MULTIBOOT2_SEARCH_LIMIT 32768u
#define URL_BUF_SIZE 256u
#define URL_PATH_BUF_SIZE 192u
#define HTTP_TIMEOUT_MS 10000u
#define HTTP_REDIRECT_LIMIT 4
#define HTTP_REQUEST_RETRY_LIMIT 2
#define HTTP_STREAM_RECV_BYTES 2048u
#define HTTP_STREAM_HEADER_BYTES 4096u
#define DOWNLOAD_PROGRESS_WIDTH 24u
#define FILE_POOL_BYTES (8u * 1024u * 1024u)

#define NET_ERR_GENERIC        -1
#define NET_ERR_UNAVAILABLE    -2
#define NET_ERR_TIMEOUT        -3
#define NET_ERR_NOT_CONFIGURED -4
#define NET_ERR_INVALID        -5
#define NET_ERR_CLOSED         -6

struct remote_kernel_url {
    uint8_t remote_ip[4];
    uint16_t remote_port;
    uint8_t secure;
    uint8_t has_explicit_port;
    uint8_t uses_host_update_port;
    char ip_text[16];
    char host[64];
    char path[URL_PATH_BUF_SIZE];
};

static uint8_t file_pool[FILE_POOL_BYTES];
static uint8_t http_stream_recv_buffer[HTTP_STREAM_RECV_BYTES];
static char http_stream_header_buffer[HTTP_STREAM_HEADER_BYTES + 1u];
static struct fat32_dirent kernel_dir_entries[64];

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

static uint16_t load_u16_le(const uint8_t *src) {
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t load_u32_le(const uint8_t *src) {
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static int ascii_casecmp(const char *a, const char *b) {
    if (!a || !b) return -1;

    while (*a && *b) {
        char ca = (char)toupper((unsigned char)*a++);
        char cb = (char)toupper((unsigned char)*b++);
        if (ca != cb) return (int)((unsigned char)ca - (unsigned char)cb);
    }

    return (int)((unsigned char)toupper((unsigned char)*a) -
                 (unsigned char)toupper((unsigned char)*b));
}

static size_t align_up_size(size_t value, size_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static int starts_with_text(const char *text, const char *prefix) {
    if (!text || !prefix) return 0;
    while (*prefix) {
        if ((char)toupper((unsigned char)*text++) !=
            (char)toupper((unsigned char)*prefix++)) {
            return 0;
        }
    }
    return 1;
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
        if (isdigit((unsigned char)*text)) {
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
        if (!isdigit((unsigned char)*text)) return 0;
        value = (value * 10u) + (uint32_t)(*text - '0');
        if (value > 65535u) return 0;
        text++;
    }
    if (value == 0) return 0;
    *out = (uint16_t)value;
    return 1;
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

static int parse_uint32_text(const char *text, uint32_t *out) {
    uint64_t value = 0;

    if (!text || !out || !*text) return 0;
    while (*text) {
        if (!isdigit((unsigned char)*text)) return 0;
        value = (value * 10u) + (uint64_t)(*text - '0');
        if (value > 0xFFFFFFFFu) return 0;
        text++;
    }

    *out = (uint32_t)value;
    return 1;
}

static int parse_header_value(const char *headers,
                              const char *name,
                              char *out,
                              size_t cap) {
    const char *line = headers;
    size_t name_len;

    if (!headers || !name || !out || cap == 0) return -1;
    name_len = strlen(name);
    out[0] = '\0';

    while (*line) {
        const char *line_end = line;
        const char *colon = 0;
        size_t pos = 0;

        while (*line_end && *line_end != '\n') {
            if (!colon && *line_end == ':') colon = line_end;
            line_end++;
        }

        if (colon && (size_t)(colon - line) == name_len &&
            strncmp(line, name, name_len) == 0) {
            const char *value = colon + 1;
            while (*value == ' ' || *value == '\t') value++;
            while (value < line_end && *value != '\r' && pos + 1 < cap) {
                out[pos++] = *value++;
            }
            out[pos] = '\0';
            return 0;
        }

        if (*line_end == '\n') line = line_end + 1;
        else break;
    }

    return -1;
}

static int parse_http_status_code_text(const char *headers, uint16_t *out_status) {
    const char *cursor;
    uint32_t status = 0;

    if (!headers || !out_status) return -1;
    cursor = headers;
    while (*cursor && *cursor != ' ') cursor++;
    while (*cursor == ' ') cursor++;
    if (!isdigit((unsigned char)cursor[0]) ||
        !isdigit((unsigned char)cursor[1]) ||
        !isdigit((unsigned char)cursor[2])) {
        return -1;
    }

    status = (uint32_t)(cursor[0] - '0') * 100u +
             (uint32_t)(cursor[1] - '0') * 10u +
             (uint32_t)(cursor[2] - '0');
    *out_status = (uint16_t)status;
    return 0;
}

static size_t find_http_header_end(const char *buf, size_t len) {
    if (!buf || len < 4) return 0;
    for (size_t i = 3; i < len; i++) {
        if (buf[i - 3] == '\r' && buf[i - 2] == '\n' &&
            buf[i - 1] == '\r' && buf[i] == '\n') {
            return i + 1;
        }
    }
    return 0;
}

static int tcp_send_all(int handle, const void *buf, size_t len, uint32_t timeout_ms) {
    const uint8_t *cursor = (const uint8_t *)buf;
    size_t sent = 0;

    while (sent < len) {
        int64_t rc = sys_net_tcp_send(handle, cursor + sent, len - sent, timeout_ms);
        if (rc <= 0) return -1;
        sent += (size_t)rc;
    }
    return 0;
}

static void write_download_progress(uint32_t done, uint32_t total, int finish_line) {
    uint32_t filled = 0;

    write_str("\rpkg: kernel download [");
    if (total != 0) {
        filled = (done >= total) ? DOWNLOAD_PROGRESS_WIDTH
                                 : (uint32_t)(((uint64_t)done * DOWNLOAD_PROGRESS_WIDTH) / total);
    }

    for (uint32_t i = 0; i < DOWNLOAD_PROGRESS_WIDTH; i++) {
        write_str(i < filled ? "#" : ".");
    }

    write_str("] ");
    if (total != 0) {
        uint32_t percent = (done >= total) ? 100u
                                           : (uint32_t)(((uint64_t)done * 100u) / total);
        write_dec(percent);
        write_str("% ");
    }
    write_dec(done);
    if (total != 0) {
        write_str("/");
        write_dec(total);
    }
    write_str(" bytes");
    if (finish_line) write_str("\n");
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
        if (c != '.' && !isdigit((unsigned char)c)) return -1;
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
        out->has_explicit_port = 1u;
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

    if (!out->secure &&
        !out->has_explicit_port &&
        out->remote_ip[0] == 10u &&
        out->remote_ip[1] == 0u &&
        out->remote_ip[2] == 2u &&
        out->remote_ip[3] == 2u &&
        starts_with_text(out->path, "/downloads/")) {
        out->remote_port = 8080u;
        out->uses_host_update_port = 1u;
    }

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

static int build_remote_url_text(const struct remote_kernel_url *url,
                                 const char *path,
                                 char *out,
                                 size_t cap) {
    size_t pos = 0;

    if (!url || !path || !out || cap == 0 || path[0] != '/') return -1;
    out[0] = '\0';

    if (append_text(out, cap, &pos, url->secure ? "https://" : "http://") != 0) return -1;
    if (append_text(out, cap, &pos, url->ip_text) != 0) return -1;
    if (!is_default_remote_port(url)) {
        if (append_char(out, cap, &pos, ':') != 0) return -1;
        if (append_port_text(out, cap, &pos, url->remote_port) != 0) return -1;
    }
    if (append_text(out, cap, &pos, path) != 0) return -1;
    return 0;
}

static int dirname_url_path(const char *path, char *out, size_t cap) {
    size_t end = 0;
    size_t last_slash = 0;

    if (!path || !out || cap == 0) return -1;
    while (path[end] && path[end] != '?' && path[end] != '#') {
        if (path[end] == '/') last_slash = end;
        end++;
    }

    if (end == 0 || path[0] != '/') {
        if (cap < 2) return -1;
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }

    if (last_slash == 0) {
        if (cap < 2) return -1;
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }

    if (last_slash + 2 > cap) return -1;
    memcpy(out, path, last_slash + 1);
    out[last_slash + 1] = '\0';
    return 0;
}

static int path_without_query(const char *path, char *out, size_t cap) {
    size_t len = 0;

    if (!path || !out || cap == 0) return -1;
    while (path[len] && path[len] != '?' && path[len] != '#') len++;
    if (len == 0 || len >= cap) return -1;
    memcpy(out, path, len);
    out[len] = '\0';
    return 0;
}

static int resolve_remote_redirect_location(const struct remote_kernel_url *base,
                                            const char *location,
                                            char *out,
                                            size_t cap) {
    char path[URL_PATH_BUF_SIZE];
    size_t pos = 0;

    if (!base || !location || !location[0] || !out || cap == 0) return -1;

    if (is_http_url(location)) {
        strncpy(out, location, cap - 1);
        out[cap - 1] = '\0';
        return location[strlen(out)] ? -1 : 0;
    }

    if (location[0] == '/') return build_remote_url_text(base, location, out, cap);

    if (location[0] == '?') {
        if (path_without_query(base->path, path, sizeof(path)) != 0) return -1;
        pos = strlen(path);
        if (pos + strlen(location) + 1 > sizeof(path)) return -1;
        memcpy(path + pos, location, strlen(location) + 1);
        return build_remote_url_text(base, path, out, cap);
    }

    if (dirname_url_path(base->path, path, sizeof(path)) != 0) return -1;
    pos = strlen(path);
    if (pos + strlen(location) + 1 > sizeof(path)) return -1;
    memcpy(path + pos, location, strlen(location) + 1);
    return build_remote_url_text(base, path, out, cap);
}

static void write_network_error_reason(int64_t rc) {
    if (rc == NET_ERR_TIMEOUT) {
        write_str("pkg: network request timed out\n");
        return;
    }
    if (rc == NET_ERR_UNAVAILABLE) {
        write_str("pkg: network driver is unavailable\n");
        return;
    }
    if (rc == NET_ERR_NOT_CONFIGURED) {
        write_str("pkg: network is not configured\n");
        return;
    }
    if (rc == NET_ERR_INVALID) {
        write_str("pkg: invalid network request\n");
        return;
    }
    if (rc == NET_ERR_CLOSED) {
        write_str("pkg: remote host closed the TCP connection\n");
        return;
    }
    write_str("pkg: network request failed\n");
}

static int request_should_retry(int64_t rc) {
    return rc == NET_ERR_TIMEOUT ||
           rc == NET_ERR_GENERIC ||
           rc == NET_ERR_CLOSED;
}

static int ensure_network_ready(void) {
    struct numos_net_info info;

    if (sys_net_info(&info) != 0 || !info.present) {
        write_str("pkg: no supported NIC detected\n");
        return -1;
    }
    if (!info.dhcp_configured) {
        write_str("pkg: network is not configured\n");
        write_str("pkg: run connect --dhcp first\n");
        return -1;
    }
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

static int read_file_prefix(const char *path, uint8_t *buf, size_t cap, size_t *out_len) {
    int fd;
    size_t total = 0;

    if (!path || !buf || cap == 0 || !out_len) return -1;
    fd = (int)sys_open(path, FAT32_O_RDONLY, 0);
    if (fd < 0) return -1;

    while (total < cap) {
        int64_t got = sys_read(fd, buf + total, cap - total);
        if (got < 0) {
            sys_close(fd);
            return -1;
        }
        if (got == 0) break;
        total += (size_t)got;
    }

    sys_close(fd);
    *out_len = total;
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

static int stream_remote_kernel_to_file(const char *url_text, const char *dst_path) {
    char current[URL_BUF_SIZE];
    char next[URL_BUF_SIZE];
    char location[URL_BUF_SIZE];
    char content_length_text[32];

    if (!url_text || !dst_path) return -1;
    if (ensure_network_ready() != 0) return -1;

    strncpy(current, url_text, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';
    if (url_text[strlen(current)] != '\0') {
        write_str("pkg: kernel URL is too long\n");
        return -1;
    }

    for (int redirect = 0; redirect <= HTTP_REDIRECT_LIMIT; redirect++) {
        struct remote_kernel_url url;
        int follow_redirect = 0;

        if (parse_remote_kernel_url(current, &url) != 0 || url.secure) {
            write_str("pkg: unsupported kernel URL\n");
            return -1;
        }
        if (url.uses_host_update_port) {
            write_str("pkg: using host update port 8080\n");
        }

        for (int attempt = 0; attempt <= HTTP_REQUEST_RETRY_LIMIT; attempt++) {
            int handle = -1;
            int dst = -1;
            int64_t recv_rc = 0;
            size_t header_len = 0;
            size_t header_end = 0;
            uint16_t status_code = 0;
            uint32_t expected_bytes = 0;
            uint32_t total_written = 0;
            int have_length = 0;
            int progress_started = 0;
            size_t req_pos = 0;
            char request_buf[512];

            request_buf[0] = '\0';
            if (append_text(request_buf, sizeof(request_buf), &req_pos, "GET ") != 0 ||
                append_text(request_buf, sizeof(request_buf), &req_pos,
                            url.path[0] ? url.path : "/") != 0 ||
                append_text(request_buf, sizeof(request_buf), &req_pos,
                            " HTTP/1.1\r\nHost: ") != 0 ||
                append_text(request_buf, sizeof(request_buf), &req_pos, url.host) != 0 ||
                append_text(request_buf, sizeof(request_buf), &req_pos,
                            "\r\nConnection: close\r\nUser-Agent: NumOS-Pkg\r\n\r\n") != 0) {
                write_str("pkg: request is too large\n");
                return -1;
            }

            handle = (int)sys_net_tcp_connect(url.remote_ip, url.remote_port, HTTP_TIMEOUT_MS);
            if (handle < 0) {
                recv_rc = handle;
                goto stream_attempt_fail;
            }
            if (tcp_send_all(handle, request_buf, req_pos, HTTP_TIMEOUT_MS) != 0) {
                recv_rc = NET_ERR_GENERIC;
                goto stream_attempt_fail;
            }

            for (;;) {
                recv_rc = sys_net_tcp_recv(handle,
                                           http_stream_recv_buffer,
                                           sizeof(http_stream_recv_buffer),
                                           HTTP_TIMEOUT_MS);
                if (recv_rc <= 0) break;

                if (header_end == 0) {
                    if (header_len + (size_t)recv_rc > HTTP_STREAM_HEADER_BYTES) {
                        write_str("pkg: HTTP headers are too large\n");
                        recv_rc = NET_ERR_INVALID;
                        goto stream_attempt_fail;
                    }
                    memcpy(http_stream_header_buffer + header_len,
                           http_stream_recv_buffer,
                           (size_t)recv_rc);
                    header_len += (size_t)recv_rc;
                    http_stream_header_buffer[header_len] = '\0';
                    header_end = find_http_header_end(http_stream_header_buffer, header_len);
                    if (header_end == 0) continue;

                    if (parse_http_status_code_text(http_stream_header_buffer, &status_code) != 0) {
                        write_str("pkg: invalid HTTP response\n");
                        recv_rc = NET_ERR_INVALID;
                        goto stream_attempt_fail;
                    }

                    if (status_code >= 300u && status_code < 400u) {
                        if (parse_header_value(http_stream_header_buffer, "Location",
                                               location, sizeof(location)) != 0 ||
                            resolve_remote_redirect_location(&url, location,
                                                             next, sizeof(next)) != 0) {
                            write_str("pkg: bad kernel redirect target\n");
                            return -1;
                        }
                        follow_redirect = 1;
                        break;
                    }

                    if (status_code < 200u || status_code >= 300u) {
                        write_str("pkg: HTTP status ");
                        write_dec(status_code);
                        write_str("\n");
                        return -1;
                    }

                    if (parse_header_value(http_stream_header_buffer, "Content-Length",
                                           content_length_text,
                                           sizeof(content_length_text)) == 0 &&
                        parse_uint32_text(content_length_text, &expected_bytes)) {
                        have_length = 1;
                    }

                    dst = (int)sys_open(dst_path,
                                        FAT32_O_WRONLY | FAT32_O_CREAT | FAT32_O_TRUNC,
                                        0);
                    if (dst < 0) {
                        write_str("pkg: failed to create staged kernel file\n");
                        return -1;
                    }

                    if (have_length) {
                        write_download_progress(0, expected_bytes, 0);
                        progress_started = 1;
                    }

                    if (header_len > header_end) {
                        size_t body_bytes = header_len - header_end;
                        if (sys_write(dst,
                                      http_stream_header_buffer + header_end,
                                      body_bytes) != (int64_t)body_bytes) {
                            write_str("pkg: failed while staging kernel bytes\n");
                            recv_rc = NET_ERR_GENERIC;
                            goto stream_attempt_fail;
                        }
                        total_written += (uint32_t)body_bytes;
                        if (have_length) write_download_progress(total_written, expected_bytes, 0);
                    }
                    continue;
                }

                if (dst < 0) {
                    write_str("pkg: missing staged kernel destination\n");
                    recv_rc = NET_ERR_INVALID;
                    goto stream_attempt_fail;
                }
                if (sys_write(dst, http_stream_recv_buffer, (size_t)recv_rc) != recv_rc) {
                    write_str("pkg: failed while staging kernel bytes\n");
                    recv_rc = NET_ERR_GENERIC;
                    goto stream_attempt_fail;
                }
                total_written += (uint32_t)recv_rc;
                if (have_length) write_download_progress(total_written, expected_bytes, 0);
            }

            if (handle >= 0) {
                sys_net_tcp_close(handle, HTTP_TIMEOUT_MS);
                handle = -1;
            }

            if (follow_redirect) {
                if (dst >= 0) sys_close(dst);
                write_str("pkg: following redirect to ");
                write_str(next);
                write_str("\n");
                strncpy(current, next, sizeof(current) - 1);
                current[sizeof(current) - 1] = '\0';
                break;
            }

            if (dst >= 0) {
                sys_close(dst);
                dst = -1;
            }

            if (recv_rc < 0) goto stream_attempt_fail;
            if (header_end == 0) {
                write_str("pkg: incomplete HTTP response\n");
                return -1;
            }
            if (have_length && total_written != expected_bytes) {
                write_str("pkg: kernel download ended early\n");
                return -1;
            }
            if (total_written == 0) {
                write_str("pkg: downloaded kernel is empty\n");
                return -1;
            }

            if (progress_started) write_download_progress(total_written, expected_bytes, 1);
            else {
                write_str("pkg: downloaded ");
                write_dec(total_written);
                write_str(" bytes\n");
            }
            return 0;

stream_attempt_fail:
            if (handle >= 0) sys_net_tcp_close(handle, HTTP_TIMEOUT_MS);
            if (dst >= 0) sys_close(dst);
            write_file_bytes(dst_path, "", 0);

            if (attempt == HTTP_REQUEST_RETRY_LIMIT || !request_should_retry(recv_rc)) {
                write_network_error_reason(recv_rc);
                return -1;
            }

            write_str("pkg: kernel download retry ");
            write_dec((uint64_t)(attempt + 2));
            write_str("/");
            write_dec((uint64_t)(HTTP_REQUEST_RETRY_LIMIT + 1));
            write_str("\n");
        }

        if (!follow_redirect) break;
        if (redirect == HTTP_REDIRECT_LIMIT) {
            write_str("pkg: too many kernel URL redirects\n");
            return -1;
        }
    }

    write_str("pkg: too many kernel URL redirects\n");
    return -1;
}

static int fetch_remote_kernel_bytes(const char *url_text,
                                     const uint8_t **out_data,
                                     size_t *out_len) {
    char current[URL_BUF_SIZE];
    char next[URL_BUF_SIZE];

    if (!url_text || !out_data || !out_len) return -1;
    if (ensure_network_ready() != 0) return -1;

    strncpy(current, url_text, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';
    if (url_text[strlen(current)] != '\0') {
        write_str("pkg: kernel URL is too long\n");
        return -1;
    }

    for (int redirect = 0; redirect <= HTTP_REDIRECT_LIMIT; redirect++) {
        struct remote_kernel_url url;
        struct numos_net_http_request request;
        struct numos_net_http_result result;
        int64_t rc = 0;

        if (parse_remote_kernel_url(current, &url) != 0) {
            write_str("pkg: unsupported kernel URL\n");
            return -1;
        }
        if (url.uses_host_update_port) {
            write_str("pkg: using host update port 8080\n");
        }

        for (int attempt = 0; attempt <= HTTP_REQUEST_RETRY_LIMIT; attempt++) {
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
            if (rc >= 0) break;
            if (attempt == HTTP_REQUEST_RETRY_LIMIT || !request_should_retry(rc)) {
                write_network_error_reason(rc);
                return -1;
            }

            write_str("pkg: kernel download retry ");
            write_dec((uint64_t)(attempt + 2));
            write_str("/");
            write_dec((uint64_t)(HTTP_REQUEST_RETRY_LIMIT + 1));
            write_str("\n");
        }

        if (result.truncated || (size_t)rc > sizeof(file_pool)) {
            write_str("pkg: kernel download is too large for staging memory\n");
            return -1;
        }

        if (result.status_code >= 200u && result.status_code < 300u) {
            *out_data = file_pool;
            *out_len = (size_t)rc;
            return 0;
        }

        if (result.status_code >= 300u && result.status_code < 400u &&
            result.location[0] != '\0') {
            if (redirect == HTTP_REDIRECT_LIMIT) {
                write_str("pkg: too many kernel URL redirects\n");
                return -1;
            }
            if (resolve_remote_redirect_location(&url, result.location,
                                                 next, sizeof(next)) != 0) {
                write_str("pkg: bad kernel redirect target\n");
                return -1;
            }
            write_str("pkg: following redirect to ");
            write_str(next);
            write_str("\n");
            strncpy(current, next, sizeof(current) - 1);
            current[sizeof(current) - 1] = '\0';
            continue;
        }

        write_str("pkg: HTTP status ");
        write_dec(result.status_code);
        write_str("\n");
        return -1;
    }

    write_str("pkg: too many kernel URL redirects\n");
    return -1;
}

static int stage_kernel_from_source(const char *src_path, const char *dst_path) {
    if (!src_path || !dst_path) return -1;

    if (is_http_url(src_path)) {
        write_str("pkg: downloading kernel from ");
        write_str(src_path);
        write_str("\n");

        if (starts_with_text(src_path, "http://")) {
            return stream_remote_kernel_to_file(src_path, dst_path);
        }

        {
            const uint8_t *downloaded = 0;
            size_t downloaded_len = 0;

            if (fetch_remote_kernel_bytes(src_path, &downloaded, &downloaded_len) != 0) {
                return -1;
            }
            if (downloaded_len == 0) {
                write_str("pkg: downloaded kernel is empty\n");
                return -1;
            }
            return write_file_bytes(dst_path, (const char *)downloaded, downloaded_len);
        }
    }

    return copy_file(src_path, dst_path);
}

static int parse_boot_cfg_value(const char *text, const char *key, char *out, size_t cap) {
    size_t key_len;
    const char *cursor;

    if (!text || !key || !out || cap == 0) return -1;
    key_len = strlen(key);
    cursor = text;

    while (*cursor) {
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
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

static int parse_cmdline_value(const char *text, const char *key, char *out, size_t cap) {
    size_t key_len;
    const char *cursor;

    if (!text || !key || !out || cap == 0) return -1;
    key_len = strlen(key);
    cursor = text;

    while (*cursor) {
        size_t pos = 0;
        const char *token;

        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (!*cursor) break;

        token = cursor;
        while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
        if ((size_t)(cursor - token) <= key_len) continue;
        if (strncmp(token, key, key_len) != 0) continue;

        token += key_len;
        while (token < cursor && pos + 1 < cap) {
            out[pos++] = *token++;
        }
        out[pos] = '\0';
        return 0;
    }

    return -1;
}

static int normalize_gfx_mode(const char *value, char *out, size_t cap) {
    if (!value || !out || cap == 0) return -1;
    if (ascii_casecmp(value, "vga") == 0) {
        strncpy(out, "vga", cap - 1);
        out[cap - 1] = '\0';
        return 0;
    }
    if (ascii_casecmp(value, "vesa") == 0) {
        strncpy(out, "vesa", cap - 1);
        out[cap - 1] = '\0';
        return 0;
    }
    if (ascii_casecmp(value, "bga") == 0) {
        strncpy(out, "bga", cap - 1);
        out[cap - 1] = '\0';
        return 0;
    }
    if (ascii_casecmp(value, "auto") == 0) {
        strncpy(out, "auto", cap - 1);
        out[cap - 1] = '\0';
        return 0;
    }
    return -1;
}

static int resolve_running_gfx_mode(char *out, size_t cap) {
    char cmdline[256];
    char value[16];

    if (!out || cap == 0) return -1;
    if (sys_get_cmdline(cmdline, sizeof(cmdline)) < 0 || cmdline[0] == '\0') return -1;
    if (parse_cmdline_value(cmdline, "gfx=", value, sizeof(value)) != 0) return -1;
    return normalize_gfx_mode(value, out, cap);
}

static int kernel_has_multiboot_framebuffer_tag(const uint8_t *data, size_t len) {
    size_t limit;

    if (!data || len < 16) return 0;
    limit = len;
    if (limit > MULTIBOOT2_SEARCH_LIMIT) limit = MULTIBOOT2_SEARCH_LIMIT;

    for (size_t off = 0; off + 16 <= limit; off += 4) {
        size_t tags_off;
        size_t header_end;

        if (load_u32_le(data + off) != MULTIBOOT2_HEADER_MAGIC) continue;

        header_end = off + (size_t)load_u32_le(data + off + 8);
        if (header_end > limit || header_end < off + 16) continue;

        tags_off = off + 16;
        while (tags_off + 8 <= header_end) {
            uint16_t type = load_u16_le(data + tags_off);
            uint32_t size = load_u32_le(data + tags_off + 4);

            if (size < 8) break;
            if (type == MULTIBOOT2_TAG_FRAMEBUFFER) return 1;
            if (type == MULTIBOOT2_TAG_END) break;

            tags_off += align_up_size((size_t)size, 8);
        }
    }

    return 0;
}

static int staged_kernel_supports_vesa(const char *path) {
    uint8_t header[MULTIBOOT2_SEARCH_LIMIT];
    size_t header_len = 0;

    if (!path) return 0;
    if (read_file_prefix(path, header, sizeof(header), &header_len) != 0) return 0;
    return kernel_has_multiboot_framebuffer_tag(header, header_len);
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
    if ((char)toupper((unsigned char)name[0]) != 'K' ||
        (char)toupper((unsigned char)name[1]) != 'E' ||
        (char)toupper((unsigned char)name[2]) != 'R' ||
        (char)toupper((unsigned char)name[3]) != 'N') {
        return -1;
    }

    for (int i = 4; i < 8; i++) {
        char c = name[i];
        if (c < '0' || c > '9') return -1;
        value = (value * 10u) + (unsigned)(c - '0');
    }

    if (name[8] != '.' ||
        (char)toupper((unsigned char)name[9]) != 'B' ||
        (char)toupper((unsigned char)name[10]) != 'I' ||
        (char)toupper((unsigned char)name[11]) != 'N' ||
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

static int select_kernel_install_path(char *out, size_t cap, int *reused_empty_slot) {
    int reusable_version = 0;
    int highest_version = 0;
    int64_t count = sys_listdir("/boot", kernel_dir_entries, 64);

    if (count < 0) return -1;

    for (int i = 0; i < count; i++) {
        int version;

        if (kernel_dir_entries[i].attr & FAT32_ATTR_DIRECTORY) continue;
        version = parse_kernel_version(kernel_dir_entries[i].name);
        if (version <= 0) continue;

        if (kernel_dir_entries[i].size == 0) {
            if (reusable_version == 0 || version < reusable_version) {
                reusable_version = version;
            }
            continue;
        }

        if (version > highest_version) highest_version = version;
    }

    if (reused_empty_slot) *reused_empty_slot = 0;

    if (reusable_version > 0) {
        if (reused_empty_slot) *reused_empty_slot = 1;
        return build_kernel_path((unsigned)reusable_version, out, cap);
    }

    if (highest_version >= (int)MAX_KERNEL_VERSION) return -1;
    return build_kernel_path((unsigned)(highest_version + 1), out, cap);
}

int pkg_install_kernel(const char *src_path, int reboot_after) {
    char boot_cfg[512];
    char current_default[64];
    char current_fallback[64];
    char default_gfx[16];
    char fallback_gfx[16];
    char running_gfx[16];
    char new_kernel_path[64];
    char fallback_kernel[64];
    int reused_empty_slot = 0;
    int have_running_gfx = 0;

    if (!src_path || src_path[0] == '\0') {
        write_str("pkg: missing kernel path\n");
        return 1;
    }

    if (sys_listdir("/boot", kernel_dir_entries, 64) < 0) {
        write_str("pkg: missing /boot directory\n");
        return 1;
    }
    if (sys_listdir("/boot/grub", kernel_dir_entries, 64) < 0) {
        write_str("pkg: missing /boot/grub directory\n");
        return 1;
    }

    if (select_kernel_install_path(new_kernel_path, sizeof(new_kernel_path),
                                   &reused_empty_slot) != 0) {
        write_str("pkg: failed to choose kernel path in /boot\n");
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

    if (resolve_running_gfx_mode(running_gfx, sizeof(running_gfx)) == 0) {
        have_running_gfx = 1;
        strncpy(default_gfx, running_gfx, sizeof(default_gfx) - 1);
        default_gfx[sizeof(default_gfx) - 1] = '\0';
        strncpy(fallback_gfx, running_gfx, sizeof(fallback_gfx) - 1);
        fallback_gfx[sizeof(fallback_gfx) - 1] = '\0';
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

    write_str("pkg: staging kernel ");
    write_str(src_path);
    write_str(" -> ");
    write_str(new_kernel_path);
    write_str("\n");
    if (reused_empty_slot) {
        write_str("pkg: reusing empty kernel slot from an earlier failed download\n");
    }

    if (stage_kernel_from_source(src_path, new_kernel_path) != 0) {
        write_str("pkg: failed to stage new kernel into /boot\n");
        return 1;
    }

    if (ascii_casecmp(default_gfx, "vesa") == 0 &&
        !staged_kernel_supports_vesa(new_kernel_path)) {
        write_file_bytes(new_kernel_path, "", 0);
        write_str("pkg: staged kernel does not support VESA boot\n");
        write_str("pkg: use a kernel image built with the framebuffer tag\n");
        return 1;
    }

    if (have_running_gfx) {
        write_str("pkg: preserving gfx mode ");
        write_str(default_gfx);
        write_str("\n");
    }

    if (write_boot_cfg(new_kernel_path, fallback_kernel, default_gfx, fallback_gfx) != 0) {
        write_str("pkg: failed to update /boot/boot.cfg\n");
        return 1;
    }
    if (write_boot_status("pending") != 0) {
        write_str("pkg: failed to update /boot/status.cfg\n");
        return 1;
    }

    write_str("pkg: kernel staged\n");
    write_str("pkg: pending boot marked, old kernel kept as fallback\n");

    if (reboot_after) {
        write_str("pkg: rebooting\n");
        sys_reboot();
    } else {
        write_str("pkg: reboot to activate the new kernel\n");
    }

    return 0;
}
