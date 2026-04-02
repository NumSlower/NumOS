#include "codec.h"

#define NUMLOSS_CMDLINE_CAP 256
#define NUMLOSS_ARG_CAP 4
static uint8_t g_input_buf[NUMLOSS_MAX_ARCHIVE_BYTES];
static uint8_t g_output_buf[NUMLOSS_MAX_ARCHIVE_BYTES];

static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void write_str(const char *s) {
    sys_write(FD_STDOUT, s, strlen(s));
}

static void write_ch(char c) {
    sys_write(FD_STDOUT, &c, 1);
}

static void write_dec(uint64_t value) {
    char buf[32];
    char tmp[32];
    int pos = 0;
    int t = 0;

    if (value == 0) {
        write_ch('0');
        return;
    }

    while (value > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (t > 0) buf[pos++] = tmp[--t];
    sys_write(FD_STDOUT, buf, (size_t)pos);
}

static void write_percent_tenths(uint32_t part, uint32_t whole) {
    uint32_t scaled = 0;

    if (whole == 0u) {
        write_str("0.0");
        return;
    }

    scaled = (part * 1000u) / whole;
    write_dec(scaled / 10u);
    write_ch('.');
    write_dec(scaled % 10u);
}

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static uint32_t read_u32_le(const uint8_t *in) {
    return (uint32_t)in[0] |
           ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

static void write_u32_le(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
    out[2] = (uint8_t)((value >> 16) & 0xffu);
    out[3] = (uint8_t)((value >> 24) & 0xffu);
}

static uint8_t archive_version(const uint8_t *header, uint32_t size) {
    if (!header || size < NUMLOSS_HEADER_SIZE) return 0u;
    if (header[0] != 'N' || header[1] != 'M' ||
        header[2] != 'L' || header[3] != 'S') {
        return 0u;
    }
    return header[4];
}

static const char *transform_name(uint8_t transform) {
    if (transform == NUMLOSS_TRANSFORM_RAW) return "raw";
    if (transform == NUMLOSS_TRANSFORM_DELTA8) return "delta8";
    if (transform == NUMLOSS_TRANSFORM_XOR8) return "xor8";
    if (transform == NUMLOSS_TRANSFORM_GROUP4) return "group4";
    if (transform == NUMLOSS_TRANSFORM_GROUP4_DELTA8) return "group4+delta8";
    if (transform == NUMLOSS_TRANSFORM_GROUP4_XOR8) return "group4+xor8";
    if (transform == NUMLOSS_TRANSFORM_GROUP8) return "group8";
    if (transform == NUMLOSS_TRANSFORM_GROUP8_DELTA8) return "group8+delta8";
    if (transform == NUMLOSS_TRANSFORM_GROUP8_XOR8) return "group8+xor8";
    return "unknown";
}

static void write_stream_header(uint8_t *out, uint32_t original_size, uint32_t chunk_size) {
    out[0] = 'N';
    out[1] = 'M';
    out[2] = 'L';
    out[3] = 'S';
    out[4] = NUMLOSS_VERSION_V2;
    out[5] = 0;
    out[6] = 0;
    out[7] = 0;
    write_u32_le(out + 8, original_size);
    write_u32_le(out + 12, chunk_size);
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int split_args(char *line, char *argv[], int cap) {
    int argc = 0;

    while (*line && argc < cap) {
        while (*line && is_space(*line)) line++;
        if (!*line) break;

        argv[argc++] = line;
        while (*line && !is_space(*line)) line++;
        if (!*line) break;
        *line++ = '\0';
    }

    return argc;
}

static const char *find_extension(const char *path) {
    const char *dot = 0;

    while (*path) {
        if (*path == '/' || *path == '\\') dot = 0;
        if (*path == '.') dot = path;
        path++;
    }

    return dot;
}

static int has_nls_extension(const char *path) {
    const char *ext = find_extension(path);

    if (!ext) return 0;
    return str_eq(ext, ".nls") || str_eq(ext, ".NLS");
}

static int build_default_compress_path(const char *input_path,
                                       char *output_path,
                                       uint32_t cap) {
    const char *ext = find_extension(input_path);
    uint32_t base_len = 0;
    uint32_t pos = 0;

    if (!input_path || !output_path || cap == 0u) return -1;
    output_path[0] = '\0';

    if (ext) {
        base_len = (uint32_t)(ext - input_path);
    } else {
        base_len = (uint32_t)strlen(input_path);
    }

    if (base_len + 5u >= cap) return -1;

    while (pos < base_len) {
        output_path[pos] = input_path[pos];
        pos++;
    }

    output_path[pos++] = '.';
    output_path[pos++] = 'n';
    output_path[pos++] = 'l';
    output_path[pos++] = 's';
    output_path[pos] = '\0';
    return 0;
}

static int build_default_decompress_path(const char *input_path,
                                         char *output_path,
                                         uint32_t cap) {
    const char *ext = find_extension(input_path);
    uint32_t base_len = 0;
    uint32_t pos = 0;

    if (!input_path || !output_path || cap == 0u || !ext) return -1;

    base_len = (uint32_t)(ext - input_path);
    if (base_len == 0u || base_len >= cap) return -1;

    while (pos < base_len) {
        output_path[pos] = input_path[pos];
        pos++;
    }

    output_path[pos] = '\0';
    return 0;
}

static void print_usage(void) {
    write_str("numloss <input>\n");
    write_str("numloss c <input> <output>\n");
    write_str("numloss d <input> <output>\n");
    write_str("numloss i <archive>\n");
    write_str("one input writes <name>.nls\n");
    write_str("compressed ELF files still run through the kernel loader\n");
    write_str("large inputs stream automatically\n");
}

static int write_all_fd(int fd, const uint8_t *buf, uint32_t size) {
    uint32_t total = 0;

    while (total < size) {
        int64_t wrote = sys_write(fd, buf + total, size - total);
        if (wrote <= 0) return -1;
        total += (uint32_t)wrote;
    }

    return 0;
}

static int read_exact(int fd, uint8_t *buf, uint32_t size) {
    uint32_t total = 0;

    while (total < size) {
        int64_t got = sys_read(fd, buf + total, size - total);
        if (got <= 0) return -1;
        total += (uint32_t)got;
    }

    return 0;
}

static int try_read_exact(int fd, uint8_t *buf, uint32_t size, uint32_t *got_total) {
    uint32_t total = 0;

    while (total < size) {
        int64_t got = sys_read(fd, buf + total, size - total);
        if (got < 0) return -1;
        if (got == 0) {
            if (got_total) *got_total = total;
            return (total == 0u) ? 1 : -1;
        }
        total += (uint32_t)got;
    }

    if (got_total) *got_total = total;
    return 0;
}

static int read_file(const char *path, uint8_t *buf, uint32_t cap, uint32_t *out_size) {
    int fd = (int)sys_open(path, FAT32_O_RDONLY, 0);
    uint32_t total = 0;

    if (fd < 0) return -1;

    for (;;) {
        int64_t got = sys_read(fd, buf + total, cap - total);
        if (got < 0) {
            sys_close(fd);
            return -1;
        }
        if (got == 0) break;

        total += (uint32_t)got;
        if (total == cap) {
            uint8_t extra = 0;
            if (sys_read(fd, &extra, 1) > 0) {
                sys_close(fd);
                return -2;
            }
            break;
        }
    }

    sys_close(fd);
    if (out_size) *out_size = total;
    return 0;
}

static int scan_file_size(const char *path, uint32_t *out_size) {
    int fd = (int)sys_open(path, FAT32_O_RDONLY, 0);
    uint32_t total = 0;

    if (fd < 0) return -1;

    for (;;) {
        int64_t got = sys_read(fd, g_input_buf, NUMLOSS_MAX_INPUT_BYTES);
        if (got < 0) {
            sys_close(fd);
            return -1;
        }
        if (got == 0) break;
        if ((uint64_t)total + (uint64_t)got > 0xffffffffu) {
            sys_close(fd);
            return -2;
        }
        total += (uint32_t)got;
    }

    sys_close(fd);
    if (out_size) *out_size = total;
    return 0;
}

static int consume_exact(int fd, uint32_t size) {
    while (size > 0u) {
        uint32_t chunk = min_u32(size, NUMLOSS_MAX_INPUT_BYTES);
        if (read_exact(fd, g_input_buf, chunk) != 0) return -1;
        size -= chunk;
    }
    return 0;
}

static int count_remaining_bytes(int fd, uint32_t *out_size) {
    uint32_t total = 0;

    for (;;) {
        int64_t got = sys_read(fd, g_input_buf, NUMLOSS_MAX_INPUT_BYTES);
        if (got < 0) return -1;
        if (got == 0) break;
        if ((uint64_t)total + (uint64_t)got > 0xffffffffu) return -1;
        total += (uint32_t)got;
    }

    if (out_size) *out_size = total;
    return 0;
}

static int write_file(const char *path, const uint8_t *buf, uint32_t size) {
    int fd = (int)sys_open(path, FAT32_O_WRONLY | FAT32_O_CREAT | FAT32_O_TRUNC, 0);
    int rc = 0;

    if (fd < 0) return -1;
    rc = write_all_fd(fd, buf, size);
    sys_close(fd);
    return rc;
}

static void print_encode_stats(uint32_t input_size, uint32_t archive_size) {
    write_str("input: ");
    write_dec(input_size);
    write_str(" bytes\n");
    write_str("archive: ");
    write_dec(archive_size);
    write_str(" bytes\n");
    write_str("archive ratio: ");
    write_percent_tenths(archive_size, input_size);
    write_str("%\n");

    if (archive_size < input_size) {
        write_str("saved: ");
        write_dec(input_size - archive_size);
        write_str(" bytes\n");
    } else {
        write_str("overhead: ");
        write_dec(archive_size - input_size);
        write_str(" bytes\n");
    }
}

static void print_decode_stats(uint32_t archive_size, uint32_t output_size) {
    write_str("archive: ");
    write_dec(archive_size);
    write_str(" bytes\n");
    write_str("output: ");
    write_dec(output_size);
    write_str(" bytes\n");
    write_str("expanded ratio: ");
    write_percent_tenths(output_size, archive_size);
    write_str("%\n");
}

static void print_codec_error(const char *label, int rc) {
    write_str(label);
    write_str(": ");

    if (rc == NUMLOSS_ERR_ARGS) {
        write_str("bad arguments\n");
        return;
    }
    if (rc == NUMLOSS_ERR_INPUT) {
        write_str("input is too large\n");
        return;
    }
    if (rc == NUMLOSS_ERR_OUTPUT) {
        write_str("output buffer is too small\n");
        return;
    }
    if (rc == NUMLOSS_ERR_FORMAT) {
        write_str("invalid numloss stream\n");
        return;
    }

    write_str("unknown error\n");
}

static int cmd_compress_small(const char *input_path, const char *output_path) {
    uint32_t input_size = 0;
    uint32_t archive_size = 0;
    int rc = 0;

    rc = read_file(input_path, g_input_buf, NUMLOSS_MAX_INPUT_BYTES, &input_size);
    if (rc == -1) {
        write_str("numloss: failed to read input\n");
        return 1;
    }
    if (rc == -2) {
        write_str("numloss: input exceeds single archive size\n");
        return 1;
    }

    rc = numloss_encode(g_input_buf, input_size,
                        g_output_buf, NUMLOSS_MAX_ARCHIVE_BYTES,
                        &archive_size);
    if (rc != NUMLOSS_OK) {
        print_codec_error("numloss", rc);
        return 1;
    }

    if (write_file(output_path, g_output_buf, archive_size) != 0) {
        write_str("numloss: failed to write output\n");
        return 1;
    }

    print_encode_stats(input_size, archive_size);
    return 0;
}

static int cmd_compress_chunked(const char *input_path,
                                const char *output_path,
                                uint32_t input_size) {
    int in_fd = -1;
    int out_fd = -1;
    uint32_t archive_size = NUMLOSS_HEADER_SIZE;

    in_fd = (int)sys_open(input_path, FAT32_O_RDONLY, 0);
    if (in_fd < 0) {
        write_str("numloss: failed to read input\n");
        return 1;
    }

    out_fd = (int)sys_open(output_path, FAT32_O_WRONLY | FAT32_O_CREAT | FAT32_O_TRUNC, 0);
    if (out_fd < 0) {
        sys_close(in_fd);
        write_str("numloss: failed to write output\n");
        return 1;
    }

    write_stream_header(g_output_buf, input_size, NUMLOSS_MAX_INPUT_BYTES);
    if (write_all_fd(out_fd, g_output_buf, NUMLOSS_HEADER_SIZE) != 0) {
        sys_close(out_fd);
        sys_close(in_fd);
        write_str("numloss: failed to write output\n");
        return 1;
    }

    for (;;) {
        int64_t got = sys_read(in_fd, g_input_buf, NUMLOSS_MAX_INPUT_BYTES);
        uint32_t chunk_archive_size = 0;
        int rc = 0;

        if (got < 0) {
            sys_close(out_fd);
            sys_close(in_fd);
            write_str("numloss: failed to read input\n");
            return 1;
        }
        if (got == 0) break;

        rc = numloss_encode(g_input_buf, (uint32_t)got,
                            g_output_buf, NUMLOSS_MAX_ARCHIVE_BYTES,
                            &chunk_archive_size);
        if (rc != NUMLOSS_OK) {
            sys_close(out_fd);
            sys_close(in_fd);
            print_codec_error("numloss", rc);
            return 1;
        }

        if (write_all_fd(out_fd, g_output_buf, chunk_archive_size) != 0) {
            sys_close(out_fd);
            sys_close(in_fd);
            write_str("numloss: failed to write output\n");
            return 1;
        }

        archive_size += chunk_archive_size;
    }

    sys_close(out_fd);
    sys_close(in_fd);
    print_encode_stats(input_size, archive_size);
    return 0;
}

static int cmd_compress(const char *input_path, const char *output_path) {
    uint32_t input_size = 0;
    int rc = scan_file_size(input_path, &input_size);

    if (rc == -1) {
        write_str("numloss: failed to read input\n");
        return 1;
    }
    if (rc == -2) {
        write_str("numloss: input is too large for this format\n");
        return 1;
    }

    if (input_size <= NUMLOSS_MAX_INPUT_BYTES) {
        return cmd_compress_small(input_path, output_path);
    }

    return cmd_compress_chunked(input_path, output_path, input_size);
}

static int cmd_decompress(const char *input_path, const char *output_path) {
    int in_fd = -1;
    int out_fd = -1;
    uint8_t header[NUMLOSS_HEADER_SIZE];
    uint8_t version = 0;
    uint32_t archive_size = 0;
    uint32_t output_size = 0;

    in_fd = (int)sys_open(input_path, FAT32_O_RDONLY, 0);
    if (in_fd < 0) {
        write_str("numloss: failed to read input\n");
        return 1;
    }
    if (read_exact(in_fd, header, NUMLOSS_HEADER_SIZE) != 0) {
        sys_close(in_fd);
        write_str("numloss: failed to read input\n");
        return 1;
    }
    archive_size = NUMLOSS_HEADER_SIZE;

    version = archive_version(header, sizeof(header));
    if (version == 0u) {
        sys_close(in_fd);
        write_str("numloss: invalid numloss stream\n");
        return 1;
    }

    out_fd = (int)sys_open(output_path, FAT32_O_WRONLY | FAT32_O_CREAT | FAT32_O_TRUNC, 0);
    if (out_fd < 0) {
        sys_close(in_fd);
        write_str("numloss: failed to write output\n");
        return 1;
    }

    if (version == NUMLOSS_VERSION_V1 || version == NUMLOSS_VERSION_V3) {
        uint32_t original_size = 0;
        uint32_t payload_size = 0;
        uint8_t extra = 0;
        int rc = 0;

        if (numloss_read_header(header, sizeof(header), &original_size, &payload_size) != NUMLOSS_OK ||
            original_size > NUMLOSS_MAX_ARCHIVE_BYTES ||
            NUMLOSS_HEADER_SIZE + payload_size > NUMLOSS_MAX_ARCHIVE_BYTES) {
            sys_close(out_fd);
            sys_close(in_fd);
            write_str("numloss: invalid numloss stream\n");
            return 1;
        }

        memcpy(g_input_buf, header, NUMLOSS_HEADER_SIZE);
        if (read_exact(in_fd, g_input_buf + NUMLOSS_HEADER_SIZE, payload_size) != 0) {
            sys_close(out_fd);
            sys_close(in_fd);
            write_str("numloss: failed to read input\n");
            return 1;
        }
        archive_size = NUMLOSS_HEADER_SIZE + payload_size;

        rc = numloss_decode(g_input_buf, archive_size,
                            g_output_buf, NUMLOSS_MAX_ARCHIVE_BYTES,
                            &output_size);
        if (rc != NUMLOSS_OK || output_size != original_size ||
            write_all_fd(out_fd, g_output_buf, output_size) != 0) {
            sys_close(out_fd);
            sys_close(in_fd);
            write_str("numloss: invalid numloss stream\n");
            return 1;
        }
        if (sys_read(in_fd, &extra, 1) != 0) {
            sys_close(out_fd);
            sys_close(in_fd);
            write_str("numloss: invalid numloss stream\n");
            return 1;
        }
    } else if (version == NUMLOSS_VERSION_V2) {
        uint32_t original_size = read_u32_le(header + 8);

        for (;;) {
            uint32_t got = 0;
            uint32_t chunk_original = 0;
            uint32_t chunk_payload = 0;
            uint32_t chunk_output = 0;
            uint32_t chunk_size = 0;
            uint8_t chunk_version = 0;
            int rc = 0;
            int read_rc = try_read_exact(in_fd, header, NUMLOSS_HEADER_SIZE, &got);

            if (read_rc == 1) break;
            chunk_version = archive_version(header, sizeof(header));
            if (read_rc != 0 ||
                (chunk_version != NUMLOSS_VERSION_V1 && chunk_version != NUMLOSS_VERSION_V3)) {
                sys_close(out_fd);
                sys_close(in_fd);
                write_str("numloss: invalid numloss stream\n");
                return 1;
            }

            if (numloss_read_header(header, sizeof(header), &chunk_original, &chunk_payload) != NUMLOSS_OK) {
                sys_close(out_fd);
                sys_close(in_fd);
                write_str("numloss: invalid numloss stream\n");
                return 1;
            }

            chunk_size = NUMLOSS_HEADER_SIZE + chunk_payload;
            if (chunk_original > NUMLOSS_MAX_ARCHIVE_BYTES ||
                chunk_size > NUMLOSS_MAX_ARCHIVE_BYTES) {
                sys_close(out_fd);
                sys_close(in_fd);
                write_str("numloss: invalid numloss stream\n");
                return 1;
            }

            memcpy(g_input_buf, header, NUMLOSS_HEADER_SIZE);
            if (read_exact(in_fd, g_input_buf + NUMLOSS_HEADER_SIZE, chunk_payload) != 0) {
                sys_close(out_fd);
                sys_close(in_fd);
                write_str("numloss: failed to read input\n");
                return 1;
            }

            rc = numloss_decode(g_input_buf, chunk_size,
                                g_output_buf, NUMLOSS_MAX_ARCHIVE_BYTES,
                                &chunk_output);
            if (rc != NUMLOSS_OK || chunk_output != chunk_original ||
                write_all_fd(out_fd, g_output_buf, chunk_output) != 0) {
                sys_close(out_fd);
                sys_close(in_fd);
                write_str("numloss: invalid numloss stream\n");
                return 1;
            }

            archive_size += chunk_size;
            output_size += chunk_output;
        }

        if (output_size != original_size) {
            sys_close(out_fd);
            sys_close(in_fd);
            write_str("numloss: invalid numloss stream\n");
            return 1;
        }
    } else {
        sys_close(out_fd);
        sys_close(in_fd);
        write_str("numloss: invalid numloss stream\n");
        return 1;
    }

    sys_close(out_fd);
    sys_close(in_fd);
    print_decode_stats(archive_size, output_size);
    return 0;
}

static int cmd_info(const char *input_path) {
    int fd = -1;
    uint8_t header[NUMLOSS_HEADER_SIZE];
    uint8_t version = 0;

    fd = (int)sys_open(input_path, FAT32_O_RDONLY, 0);
    if (fd < 0) {
        write_str("numloss: failed to read input\n");
        return 1;
    }
    if (read_exact(fd, header, NUMLOSS_HEADER_SIZE) != 0) {
        sys_close(fd);
        write_str("numloss: failed to read input\n");
        return 1;
    }

    version = archive_version(header, sizeof(header));
    if (version == NUMLOSS_VERSION_V1) {
        uint32_t original_size = read_u32_le(header + 8);
        uint32_t payload_size = read_u32_le(header + 12);
        uint32_t actual_payload = 0;

        if (count_remaining_bytes(fd, &actual_payload) != 0 || actual_payload != payload_size) {
            sys_close(fd);
            write_str("numloss: invalid numloss stream\n");
            return 1;
        }

        write_str("format: NMLS v1\n");
        write_str("archive: ");
        write_dec(NUMLOSS_HEADER_SIZE + payload_size);
        write_str(" bytes\n");
        write_str("payload: ");
        write_dec(payload_size);
        write_str(" bytes\n");
        write_str("original: ");
        write_dec(original_size);
        write_str(" bytes\n");
        write_str("archive ratio: ");
        write_percent_tenths(NUMLOSS_HEADER_SIZE + payload_size, original_size);
        write_str("%\n");
        sys_close(fd);
        return 0;
    }

    if (version == NUMLOSS_VERSION_V3) {
        uint32_t original_size = read_u32_le(header + 8);
        uint32_t payload_size = read_u32_le(header + 12);
        uint32_t actual_payload = 0;

        if (count_remaining_bytes(fd, &actual_payload) != 0 || actual_payload != payload_size) {
            sys_close(fd);
            write_str("numloss: invalid numloss stream\n");
            return 1;
        }

        write_str("format: NMLS v3\n");
        write_str("transform: ");
        write_str(transform_name(header[5]));
        write_str("\n");
        write_str("archive: ");
        write_dec(NUMLOSS_HEADER_SIZE + payload_size);
        write_str(" bytes\n");
        write_str("payload: ");
        write_dec(payload_size);
        write_str(" bytes\n");
        write_str("original: ");
        write_dec(original_size);
        write_str(" bytes\n");
        write_str("archive ratio: ");
        write_percent_tenths(NUMLOSS_HEADER_SIZE + payload_size, original_size);
        write_str("%\n");
        sys_close(fd);
        return 0;
    }

    if (version == NUMLOSS_VERSION_V2) {
        uint32_t original_size = read_u32_le(header + 8);
        uint32_t chunk_size = read_u32_le(header + 12);
        uint32_t payload_size = 0;
        uint32_t total_chunks = 0;
        uint32_t total_original = 0;
        uint32_t v1_chunks = 0;
        uint32_t v3_chunks = 0;

        for (;;) {
            uint32_t got = 0;
            uint32_t chunk_original = 0;
            uint32_t chunk_payload = 0;
            uint8_t chunk_version = 0;
            int read_rc = try_read_exact(fd, header, NUMLOSS_HEADER_SIZE, &got);

            if (read_rc == 1) break;
            chunk_version = archive_version(header, sizeof(header));
            if (read_rc != 0 ||
                (chunk_version != NUMLOSS_VERSION_V1 && chunk_version != NUMLOSS_VERSION_V3)) {
                sys_close(fd);
                write_str("numloss: invalid numloss stream\n");
                return 1;
            }

            chunk_original = read_u32_le(header + 8);
            chunk_payload = read_u32_le(header + 12);
            if (consume_exact(fd, chunk_payload) != 0) {
                sys_close(fd);
                write_str("numloss: invalid numloss stream\n");
                return 1;
            }

            payload_size += NUMLOSS_HEADER_SIZE + chunk_payload;
            total_original += chunk_original;
            total_chunks++;
            if (chunk_version == NUMLOSS_VERSION_V1) v1_chunks++;
            if (chunk_version == NUMLOSS_VERSION_V3) v3_chunks++;
        }

        if (total_original != original_size) {
            sys_close(fd);
            write_str("numloss: invalid numloss stream\n");
            return 1;
        }

        write_str("format: NMLS v2 chunked\n");
        write_str("archive: ");
        write_dec(NUMLOSS_HEADER_SIZE + payload_size);
        write_str(" bytes\n");
        write_str("payload: ");
        write_dec(payload_size);
        write_str(" bytes\n");
        write_str("original: ");
        write_dec(original_size);
        write_str(" bytes\n");
        write_str("chunks: ");
        write_dec(total_chunks);
        write_str("\n");
        write_str("chunk codecs: v1=");
        write_dec(v1_chunks);
        write_str(", v3=");
        write_dec(v3_chunks);
        write_str("\n");
        write_str("chunk size: ");
        write_dec(chunk_size);
        write_str(" bytes\n");
        write_str("archive ratio: ");
        write_percent_tenths(NUMLOSS_HEADER_SIZE + payload_size, original_size);
        write_str("%\n");
        sys_close(fd);
        return 0;
    }

    sys_close(fd);
    write_str("numloss: invalid numloss stream\n");
    return 1;
}

static int cmd_default(const char *input_path) {
    char output_path[NUMLOSS_CMDLINE_CAP];

    if (has_nls_extension(input_path)) {
        if (build_default_decompress_path(input_path, output_path,
                                          sizeof(output_path)) != 0) {
            write_str("numloss: failed to build output path\n");
            return 1;
        }
        return cmd_decompress(input_path, output_path);
    }

    if (build_default_compress_path(input_path, output_path,
                                    sizeof(output_path)) != 0) {
        write_str("numloss: failed to build output path\n");
        return 1;
    }

    return cmd_compress(input_path, output_path);
}

int main(void) {
    char cmdline[NUMLOSS_CMDLINE_CAP];
    char *argv[NUMLOSS_ARG_CAP];
    int argc = 0;

    if (sys_get_cmdline(cmdline, sizeof(cmdline)) < 0) cmdline[0] = '\0';

    argc = split_args(cmdline, argv, NUMLOSS_ARG_CAP);
    if (argc == 0) {
        print_usage();
        return 0;
    }

    if (argc == 1 &&
        strcmp(argv[0], "c") != 0 &&
        strcmp(argv[0], "compress") != 0 &&
        strcmp(argv[0], "d") != 0 &&
        strcmp(argv[0], "decompress") != 0 &&
        strcmp(argv[0], "i") != 0 &&
        strcmp(argv[0], "info") != 0) {
        return cmd_default(argv[0]);
    }

    if (strcmp(argv[0], "c") == 0 || strcmp(argv[0], "compress") == 0) {
        if (argc != 3) {
            print_usage();
            return 1;
        }
        return cmd_compress(argv[1], argv[2]);
    }

    if (strcmp(argv[0], "d") == 0 || strcmp(argv[0], "decompress") == 0) {
        if (argc != 3) {
            print_usage();
            return 1;
        }
        return cmd_decompress(argv[1], argv[2]);
    }

    if (strcmp(argv[0], "i") == 0 || strcmp(argv[0], "info") == 0) {
        if (argc != 2) {
            print_usage();
            return 1;
        }
        return cmd_info(argv[1]);
    }

    print_usage();
    return 1;
}
