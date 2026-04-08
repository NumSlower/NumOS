#include "kernel/numloss.h"
#include "kernel/kernel.h"

#define NUMLOSS_MAGIC_0 'N'
#define NUMLOSS_MAGIC_1 'M'
#define NUMLOSS_MAGIC_2 'L'
#define NUMLOSS_MAGIC_3 'S'
#define NUMLOSS_RUN_MIN_V3 3u
#define NUMLOSS_MATCH_MIN_V3 4u
#define NUMLOSS_SHORT_MATCH_MIN 4u
#define NUMLOSS_SHORT_MATCH_MAX 7u
#define NUMLOSS_SHORT_MATCH_OFFSET_MAX 6144u
#define NUMLOSS_SHORT_MATCH_RANGE NUMLOSS_SHORT_MATCH_OFFSET_MAX
#define NUMLOSS_SHORT_MATCH_TOKEN_BASE 0x80u
#define NUMLOSS_SHORT_MATCH_TOKEN_LAST 0xdfu
#define NUMLOSS_REPEAT_MATCH_MIN 3u
#define NUMLOSS_REPEAT_MATCH_TOKEN_BASE 0xe0u
#define NUMLOSS_REPEAT_MATCH_TOKEN_LAST 0xfeu
#define NUMLOSS_LONG_MATCH_TOKEN 0xffu
#define NUMLOSS_VISIT_BITMAP_BYTES ((256u * 1024u + 7u) / 8u)
#define NUMLOSS_TEXT_ESCAPE 0u
#define NUMLOSS_TEXT_TOKEN_BASE 0x80u

static uint8_t g_visit_bitmap[NUMLOSS_VISIT_BITMAP_BYTES];
static uint8_t g_transform_buf[256u * 1024u];

struct numloss_text_dict_entry {
    const char *text;
    uint8_t len;
};

#define TEXT_ENTRY(s) { s, (uint8_t)(sizeof(s) - 1u) }

static const struct numloss_text_dict_entry g_text_prose_dict[] = {
    TEXT_ENTRY(" the "),
    TEXT_ENTRY(" and "),
    TEXT_ENTRY(" compress"),
    TEXT_ENTRY("ing "),
    TEXT_ENTRY("compression"),
    TEXT_ENTRY("tion"),
    TEXT_ENTRY("ompression "),
    TEXT_ENTRY(":\n\n```bash\n"),
    TEXT_ENTRY("\n\n```bash\nmake "),
    TEXT_ENTRY("\n- `"),
    TEXT_ENTRY(" of "),
    TEXT_ENTRY(" kernel"),
    TEXT_ENTRY("s th"),
    TEXT_ENTRY("ding"),
    TEXT_ENTRY(" in "),
    TEXT_ENTRY("ossless compres"),
    TEXT_ENTRY(" to "),
    TEXT_ENTRY(". The "),
    TEXT_ENTRY(" symbol"),
    TEXT_ENTRY(" with "),
    TEXT_ENTRY(" codin"),
    TEXT_ENTRY("build"),
    TEXT_ENTRY("kernel "),
    TEXT_ENTRY("\n```\n\n"),
    TEXT_ENTRY(" appear"),
    TEXT_ENTRY(" informatio"),
    TEXT_ENTRY(" lossless compre"),
    TEXT_ENTRY("boot"),
    TEXT_ENTRY(" that "),
    TEXT_ENTRY("ore "),
    TEXT_ENTRY(" use"),
    TEXT_ENTRY(" data"),
    TEXT_ENTRY("install"),
    TEXT_ENTRY(" Huffman"),
    TEXT_ENTRY(" algorithm"),
    TEXT_ENTRY(", and"),
    TEXT_ENTRY(" partitio"),
    TEXT_ENTRY("es t"),
    TEXT_ENTRY("ctio"),
    TEXT_ENTRY("rithmetic codi"),
    TEXT_ENTRY("appears "),
    TEXT_ENTRY("Huffman codi"),
    TEXT_ENTRY(" for "),
    TEXT_ENTRY("n th"),
    TEXT_ENTRY(" ins"),
    TEXT_ENTRY(" con"),
    TEXT_ENTRY(" character"),
    TEXT_ENTRY("mpression ratio"),
    TEXT_ENTRY("\n```bash\nmake p"),
    TEXT_ENTRY(" file"),
    TEXT_ENTRY("age "),
    TEXT_ENTRY(" sta"),
    TEXT_ENTRY("the s"),
    TEXT_ENTRY(" stor"),
    TEXT_ENTRY(" buil"),
    TEXT_ENTRY("haracters"),
    TEXT_ENTRY(" entropy "),
    TEXT_ENTRY(" pro"),
    TEXT_ENTRY(" is "),
    TEXT_ENTRY(" current"),
    TEXT_ENTRY("s a "),
    TEXT_ENTRY("ent "),
    TEXT_ENTRY("e in"),
    TEXT_ENTRY("ionary"),
    TEXT_ENTRY(" dictionary"),
    TEXT_ENTRY(". It "),
    TEXT_ENTRY(" compression"),
    TEXT_ENTRY(" of the "),
    TEXT_ENTRY(" coding"),
    TEXT_ENTRY(" appears "),
    TEXT_ENTRY(" Huffman coding"),
    TEXT_ENTRY(" compression ratio"),
    TEXT_ENTRY(" DEFLATE"),
    TEXT_ENTRY(" characters"),
    TEXT_ENTRY(" string"),
    TEXT_ENTRY(" code"),
    TEXT_ENTRY("Lossless compression "),
    TEXT_ENTRY(" entropy coding"),
    TEXT_ENTRY(" compresses "),
    TEXT_ENTRY(" information"),
    TEXT_ENTRY(" arithmetic coding"),
    TEXT_ENTRY(" the same "),
    TEXT_ENTRY(" identical "),
    TEXT_ENTRY(" use "),
    TEXT_ENTRY(" represent"),
    TEXT_ENTRY(" frequency"),
    TEXT_ENTRY(" compression ratios "),
    TEXT_ENTRY(" uses"),
    TEXT_ENTRY(" prediction"),
    TEXT_ENTRY(" modern "),
    TEXT_ENTRY(" lossless compression"),
    TEXT_ENTRY(" repeated strings"),
    TEXT_ENTRY(" compression algorithm"),
    TEXT_ENTRY(" compression formats"),
    TEXT_ENTRY(" compressor "),
    TEXT_ENTRY(" probabilities "),
    TEXT_ENTRY(" bandwidth "),
    TEXT_ENTRY(" storage "),
};

static const struct numloss_text_dict_entry g_text_code_dict[] = {
    TEXT_ENTRY("       "),
    TEXT_ENTRY("================"),
    TEXT_ENTRY("\n    "),
    TEXT_ENTRY(";\n   "),
    TEXT_ENTRY("----------------"),
    TEXT_ENTRY("    if ("),
    TEXT_ENTRY(" return "),
    TEXT_ENTRY(") {\n   "),
    TEXT_ENTRY(");\n  "),
    TEXT_ENTRY("    return"),
    TEXT_ENTRY("    uint"),
    TEXT_ENTRY("\n\n   "),
    TEXT_ENTRY("      if "),
    TEXT_ENTRY("    }\n"),
    TEXT_ENTRY("uint32_t"),
    TEXT_ENTRY("\n#define "),
    TEXT_ENTRY("size"),
    TEXT_ENTRY("      retur"),
    TEXT_ENTRY("int64_t"),
    TEXT_ENTRY("\nstatic "),
    TEXT_ENTRY(") return"),
    TEXT_ENTRY("int32_t "),
    TEXT_ENTRY("uint8_t "),
    TEXT_ENTRY("write"),
    TEXT_ENTRY("const char *"),
    TEXT_ENTRY("    c"),
    TEXT_ENTRY("\n}\n\nstatic"),
    TEXT_ENTRY(" uint8_t"),
    TEXT_ENTRY(" uint32_"),
    TEXT_ENTRY("    s"),
    TEXT_ENTRY("   }\n\n  "),
    TEXT_ENTRY("nt64_t "),
    TEXT_ENTRY(";\n\n  "),
    TEXT_ENTRY("   }\n   "),
    TEXT_ENTRY(";\n}\n\nstati"),
    TEXT_ENTRY("struct "),
    TEXT_ENTRY("uint64_"),
    TEXT_ENTRY(",0x00,0x00,0x00,"),
    TEXT_ENTRY(",\n   "),
    TEXT_ENTRY("   if (!"),
    TEXT_ENTRY("    writ"),
    TEXT_ENTRY("   uint32"),
    TEXT_ENTRY("(uint"),
    TEXT_ENTRY("void"),
    TEXT_ENTRY("int "),
    TEXT_ENTRY("      }"),
    TEXT_ENTRY(" = 0;"),
    TEXT_ENTRY("    vga_writ"),
    TEXT_ENTRY("x00,0x00,0x00,0x"),
    TEXT_ENTRY("0x00,0x00,0x00,0"),
    TEXT_ENTRY("   uint8_"),
    TEXT_ENTRY("ritestring("),
    TEXT_ENTRY(" */\n"),
    TEXT_ENTRY("0,0x00,0x00,0x00"),
    TEXT_ENTRY("ize_t "),
    TEXT_ENTRY("00,0x00,0x00,0x0"),
    TEXT_ENTRY(" siz"),
    TEXT_ENTRY(" 0;\n  "),
    TEXT_ENTRY("rite_str(\""),
    TEXT_ENTRY("return 0;\n"),
    TEXT_ENTRY("\");\n "),
    TEXT_ENTRY(" ==============="),
    TEXT_ENTRY("itestring(\""),
    TEXT_ENTRY(" NUMLOSS_"),
};
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

static uint8_t archive_version(const uint8_t *input, uint32_t input_size) {
    if (!input || input_size < NUMLOSS_HEADER_SIZE) return 0u;
    if (input[0] != NUMLOSS_MAGIC_0) return 0u;
    if (input[1] != NUMLOSS_MAGIC_1) return 0u;
    if (input[2] != NUMLOSS_MAGIC_2) return 0u;
    if (input[3] != NUMLOSS_MAGIC_3) return 0u;
    return input[4];
}

static uint8_t archive_transform(const uint8_t *input, uint32_t input_size) {
    uint8_t version = archive_version(input, input_size);
    if (version != NUMLOSS_VERSION_V3 && version != NUMLOSS_VERSION_V4) return NUMLOSS_TRANSFORM_RAW;
    return input[5];
}

int numloss_is_archive(const uint8_t *input, uint32_t input_size) {
    uint8_t version = archive_version(input, input_size);
    return version == NUMLOSS_VERSION_V1 ||
           version == NUMLOSS_VERSION_V2 ||
           version == NUMLOSS_VERSION_V3 ||
           version == NUMLOSS_VERSION_V4;
}

int numloss_read_header(const uint8_t *input, uint32_t input_size,
                        uint32_t *original_size, uint32_t *payload_size) {
    uint8_t version = 0;
    uint32_t payload = 0;

    version = archive_version(input, input_size);
    if (version == 0u) return NUMLOSS_ERR_FORMAT;

    if (version == NUMLOSS_VERSION_V1 ||
        version == NUMLOSS_VERSION_V3 ||
        version == NUMLOSS_VERSION_V4) {
        payload = read_u32_le(input + 12);
        if (payload > input_size - NUMLOSS_HEADER_SIZE) return NUMLOSS_ERR_FORMAT;
    } else if (version == NUMLOSS_VERSION_V2) {
        payload = input_size - NUMLOSS_HEADER_SIZE;
    } else {
        return NUMLOSS_ERR_FORMAT;
    }

    if (original_size) *original_size = read_u32_le(input + 8);
    if (payload_size) *payload_size = payload;
    return NUMLOSS_OK;
}

static const struct numloss_text_dict_entry *text_dictionary_for_transform(uint8_t transform,
                                                                           uint32_t *count_out) {
    if (transform == NUMLOSS_TRANSFORM_TEXT_PROSE) {
        if (count_out) *count_out = (uint32_t)(sizeof(g_text_prose_dict) / sizeof(g_text_prose_dict[0]));
        return g_text_prose_dict;
    }

    if (transform == NUMLOSS_TRANSFORM_TEXT_CODE) {
        if (count_out) *count_out = (uint32_t)(sizeof(g_text_code_dict) / sizeof(g_text_code_dict[0]));
        return g_text_code_dict;
    }

    if (count_out) *count_out = 0u;
    return 0;
}

static void inverse_delta_in_place(uint8_t *data, uint32_t input_size) {
    uint8_t prev = 0u;

    for (uint32_t index = 0; index < input_size; index++) {
        prev = (uint8_t)(prev + data[index]);
        data[index] = prev;
    }
}

static void inverse_xor_in_place(uint8_t *data, uint32_t input_size) {
    uint8_t prev = 0u;

    for (uint32_t index = 0; index < input_size; index++) {
        prev = (uint8_t)(prev ^ data[index]);
        data[index] = prev;
    }
}

static void inverse_delta2_in_place(uint8_t *data, uint32_t input_size) {
    uint8_t delta = 0u;
    uint8_t value = 0u;

    for (uint32_t index = 0; index < input_size; index++) {
        delta = (uint8_t)(delta + data[index]);
        value = (uint8_t)(value + delta);
        data[index] = value;
    }
}

static void inverse_delta32le_in_place(uint8_t *data, uint32_t input_size) {
    uint32_t prev = 0u;
    uint32_t full_words = input_size / 4u;

    for (uint32_t index = 0u; index < full_words; index++) {
        uint32_t offset = index * 4u;
        uint32_t delta = read_u32_le(data + offset);

        prev += delta;
        write_u32_le(data + offset, prev);
    }
}

static void clear_visit_bitmap(uint32_t bits) {
    uint32_t bytes = (bits + 7u) / 8u;
    memset(g_visit_bitmap, 0, bytes);
}

static int visit_bitmap_get(uint32_t index) {
    return (g_visit_bitmap[index >> 3] & (uint8_t)(1u << (index & 7u))) != 0;
}

static void visit_bitmap_set(uint32_t index) {
    g_visit_bitmap[index >> 3] |= (uint8_t)(1u << (index & 7u));
}

static uint32_t group_inverse_target(uint32_t index, uint32_t full_words, uint32_t width) {
    uint32_t head_size = full_words * width;
    uint32_t lane = 0;
    uint32_t row = 0;

    if (index >= head_size) return index;

    lane = index / full_words;
    row = index % full_words;
    return row * width + lane;
}

static void inverse_group_in_place(uint8_t *data, uint32_t input_size, uint32_t width) {
    uint32_t full_words = input_size / width;
    uint32_t head_size = full_words * width;

    if (full_words == 0u || head_size == 0u) return;

    clear_visit_bitmap(head_size);

    for (uint32_t start = 0; start < head_size; start++) {
        uint32_t next = 0;
        uint32_t cur = 0;
        uint8_t temp = 0;

        if (visit_bitmap_get(start)) continue;

        next = group_inverse_target(start, full_words, width);
        if (next == start) {
            visit_bitmap_set(start);
            continue;
        }

        temp = data[start];
        cur = start;
        for (;;) {
            uint8_t saved = 0;

            visit_bitmap_set(cur);
            next = group_inverse_target(cur, full_words, width);
            saved = data[next];
            data[next] = temp;
            temp = saved;
            cur = next;
            if (cur == start) break;
        }
    }
}

static int inverse_text_dictionary_transform(const uint8_t *input, uint32_t input_size,
                                             const struct numloss_text_dict_entry *dict,
                                             uint32_t dict_count,
                                             uint8_t *output, uint32_t output_cap,
                                             uint32_t *output_size) {
    uint32_t in_pos = 0u;
    uint32_t out_pos = 0u;

    while (in_pos < input_size) {
        uint8_t token = input[in_pos++];

        if (token == NUMLOSS_TEXT_ESCAPE) {
            if (in_pos >= input_size || out_pos + 1u > output_cap) return NUMLOSS_ERR_FORMAT;
            output[out_pos++] = input[in_pos++];
            continue;
        }

        if (token < NUMLOSS_TEXT_TOKEN_BASE) {
            if (out_pos + 1u > output_cap) return NUMLOSS_ERR_OUTPUT;
            output[out_pos++] = token;
            continue;
        }

        {
            uint32_t index = (uint32_t)(token - NUMLOSS_TEXT_TOKEN_BASE);
            uint32_t len = 0u;

            if (index >= dict_count) return NUMLOSS_ERR_FORMAT;
            len = dict[index].len;
            if (out_pos + len > output_cap) return NUMLOSS_ERR_OUTPUT;
            memcpy(output + out_pos, dict[index].text, len);
            out_pos += len;
        }
    }

    if (output_size) *output_size = out_pos;
    return NUMLOSS_OK;
}

static int inverse_transform_in_place(uint8_t *data, uint32_t input_size, uint8_t transform) {
    if (transform == NUMLOSS_TRANSFORM_RAW) return NUMLOSS_OK;

    if (transform == NUMLOSS_TRANSFORM_DELTA8) {
        inverse_delta_in_place(data, input_size);
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_XOR8) {
        inverse_xor_in_place(data, input_size);
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP4) {
        inverse_group_in_place(data, input_size, 4u);
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP4_DELTA8) {
        inverse_delta_in_place(data, input_size);
        inverse_group_in_place(data, input_size, 4u);
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP4_XOR8) {
        inverse_xor_in_place(data, input_size);
        inverse_group_in_place(data, input_size, 4u);
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP8) {
        inverse_group_in_place(data, input_size, 8u);
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP8_DELTA8) {
        inverse_delta_in_place(data, input_size);
        inverse_group_in_place(data, input_size, 8u);
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP8_XOR8) {
        inverse_xor_in_place(data, input_size);
        inverse_group_in_place(data, input_size, 8u);
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP2) {
        inverse_group_in_place(data, input_size, 2u);
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP2_DELTA8) {
        inverse_delta_in_place(data, input_size);
        inverse_group_in_place(data, input_size, 2u);
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP2_XOR8) {
        inverse_xor_in_place(data, input_size);
        inverse_group_in_place(data, input_size, 2u);
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_DELTA8_DELTA8) {
        inverse_delta2_in_place(data, input_size);
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_DELTA32LE) {
        inverse_delta32le_in_place(data, input_size);
        return NUMLOSS_OK;
    }

    return NUMLOSS_ERR_FORMAT;
}

static int numloss_decode_v1(const uint8_t *input, uint32_t input_size,
                             uint8_t *output, uint32_t output_cap,
                             uint32_t *output_size) {
    uint32_t original_size = 0;
    uint32_t payload_size = 0;
    uint32_t in_pos = NUMLOSS_HEADER_SIZE;
    uint32_t in_end = 0;
    uint32_t out_pos = 0;
    int rc = NUMLOSS_OK;

    if (!input || !output || !output_size) return NUMLOSS_ERR_ARGS;

    rc = numloss_read_header(input, input_size, &original_size, &payload_size);
    if (rc != NUMLOSS_OK) return rc;

    if (archive_version(input, input_size) != NUMLOSS_VERSION_V1) return NUMLOSS_ERR_FORMAT;
    if (input_size != NUMLOSS_HEADER_SIZE + payload_size) return NUMLOSS_ERR_FORMAT;
    if (original_size > output_cap) return NUMLOSS_ERR_OUTPUT;

    in_end = NUMLOSS_HEADER_SIZE + payload_size;

    while (in_pos < in_end) {
        uint8_t token = input[in_pos++];
        uint32_t kind = (uint32_t)(token >> 6);
        uint32_t len = 0;

        if (kind == 0u) {
            len = (uint32_t)(token & 0x3fu) + 1u;
            if (in_pos + len > in_end) return NUMLOSS_ERR_FORMAT;
            if (out_pos + len > original_size || out_pos + len > output_cap) {
                return NUMLOSS_ERR_OUTPUT;
            }
            memcpy(output + out_pos, input + in_pos, len);
            in_pos += len;
            out_pos += len;
            continue;
        }

        if (kind == 1u) {
            uint8_t value = 0;

            len = (uint32_t)(token & 0x3fu) + 3u;
            if (in_pos >= in_end) return NUMLOSS_ERR_FORMAT;
            if (out_pos + len > original_size || out_pos + len > output_cap) {
                return NUMLOSS_ERR_OUTPUT;
            }

            value = input[in_pos++];
            memset(output + out_pos, value, len);
            out_pos += len;
            continue;
        }

        if (kind == 2u) {
            uint32_t offset = 0;

            len = (uint32_t)(token & 0x3fu) + 3u;
            if (in_pos + 2u > in_end) return NUMLOSS_ERR_FORMAT;

            offset = (uint32_t)input[in_pos] |
                     ((uint32_t)input[in_pos + 1u] << 8);
            in_pos += 2u;

            if (offset == 0u || offset > out_pos) return NUMLOSS_ERR_FORMAT;
            if (out_pos + len > original_size || out_pos + len > output_cap) {
                return NUMLOSS_ERR_OUTPUT;
            }

            for (uint32_t i = 0; i < len; i++) {
                output[out_pos + i] = output[out_pos - offset + i];
            }
            out_pos += len;
            continue;
        }

        return NUMLOSS_ERR_FORMAT;
    }

    if (out_pos != original_size) return NUMLOSS_ERR_FORMAT;
    *output_size = out_pos;
    return NUMLOSS_OK;
}

static int numloss_decode_match_stream(const uint8_t *payload, uint32_t payload_size,
                                       uint8_t *output, uint32_t output_cap,
                                       uint32_t *output_size) {
    uint32_t in_pos = 0u;
    uint32_t in_end = payload_size;
    uint32_t out_pos = 0;
    uint32_t last_offset = 0;

    while (in_pos < in_end) {
        uint8_t token = payload[in_pos++];

        if (token <= 0x3fu) {
            uint32_t len = (uint32_t)token + 1u;
            if (in_pos + len > in_end || out_pos + len > output_cap) return NUMLOSS_ERR_FORMAT;
            memcpy(output + out_pos, payload + in_pos, len);
            in_pos += len;
            out_pos += len;
            continue;
        }

        if (token <= 0x7fu) {
            uint32_t len = (uint32_t)(token - 0x40u) + NUMLOSS_RUN_MIN_V3;
            uint8_t value = 0;

            if (in_pos >= in_end || out_pos + len > output_cap) return NUMLOSS_ERR_FORMAT;

            value = payload[in_pos++];
            memset(output + out_pos, value, len);
            out_pos += len;
            continue;
        }

        if (token <= NUMLOSS_SHORT_MATCH_TOKEN_LAST) {
            uint32_t code = 0;
            uint32_t len = 0;
            uint32_t offset = 0;

            if (in_pos >= in_end) return NUMLOSS_ERR_FORMAT;

            code = (((uint32_t)token - NUMLOSS_SHORT_MATCH_TOKEN_BASE) << 8) | (uint32_t)payload[in_pos++];
            len = NUMLOSS_SHORT_MATCH_MIN + (code / NUMLOSS_SHORT_MATCH_RANGE);
            offset = 1u + (code % NUMLOSS_SHORT_MATCH_RANGE);

            if (len > NUMLOSS_SHORT_MATCH_MAX) return NUMLOSS_ERR_FORMAT;
            if (offset == 0u || offset > out_pos) return NUMLOSS_ERR_FORMAT;
            if (out_pos + len > output_cap) return NUMLOSS_ERR_OUTPUT;

            last_offset = offset;
            for (uint32_t i = 0; i < len; i++) {
                output[out_pos + i] = output[out_pos - offset + i];
            }
            out_pos += len;
            continue;
        }

        if (token <= NUMLOSS_REPEAT_MATCH_TOKEN_LAST) {
            uint32_t len = NUMLOSS_REPEAT_MATCH_MIN + ((uint32_t)token - NUMLOSS_REPEAT_MATCH_TOKEN_BASE);

            if (last_offset == 0u || last_offset > out_pos) return NUMLOSS_ERR_FORMAT;
            if (out_pos + len > output_cap) return NUMLOSS_ERR_OUTPUT;

            for (uint32_t i = 0; i < len; i++) {
                output[out_pos + i] = output[out_pos - last_offset + i];
            }
            out_pos += len;
            continue;
        }

        if (token != NUMLOSS_LONG_MATCH_TOKEN || in_pos + 3u > in_end) return NUMLOSS_ERR_FORMAT;

        {
            uint32_t len = NUMLOSS_MATCH_MIN_V3 + (uint32_t)payload[in_pos++];
            uint32_t offset = (uint32_t)payload[in_pos] |
                              ((uint32_t)payload[in_pos + 1u] << 8);
            in_pos += 2u;

            if (offset == 0u || offset > out_pos) return NUMLOSS_ERR_FORMAT;
            if (out_pos + len > output_cap) return NUMLOSS_ERR_OUTPUT;

            last_offset = offset;
            for (uint32_t i = 0; i < len; i++) {
                output[out_pos + i] = output[out_pos - offset + i];
            }
            out_pos += len;
        }
    }

    *output_size = out_pos;
    return NUMLOSS_OK;
}

static int numloss_decode_v3(const uint8_t *input, uint32_t input_size,
                             uint8_t *output, uint32_t output_cap,
                             uint32_t *output_size) {
    uint32_t original_size = 0;
    uint32_t payload_size = 0;
    uint8_t transform = archive_transform(input, input_size);
    int rc = NUMLOSS_OK;

    if (!input || !output || !output_size) return NUMLOSS_ERR_ARGS;

    rc = numloss_read_header(input, input_size, &original_size, &payload_size);
    if (rc != NUMLOSS_OK) return rc;

    if (archive_version(input, input_size) != NUMLOSS_VERSION_V3) return NUMLOSS_ERR_FORMAT;
    if (input_size != NUMLOSS_HEADER_SIZE + payload_size) return NUMLOSS_ERR_FORMAT;
    if (original_size > output_cap) return NUMLOSS_ERR_OUTPUT;

    rc = numloss_decode_match_stream(input + NUMLOSS_HEADER_SIZE, payload_size,
                                     output, original_size, output_size);
    if (rc != NUMLOSS_OK || *output_size != original_size) return NUMLOSS_ERR_FORMAT;

    rc = inverse_transform_in_place(output, original_size, transform);
    if (rc != NUMLOSS_OK) return rc;

    *output_size = original_size;
    return NUMLOSS_OK;
}

static int numloss_decode_v4(const uint8_t *input, uint32_t input_size,
                             uint8_t *output, uint32_t output_cap,
                             uint32_t *output_size) {
    uint32_t original_size = 0u;
    uint32_t payload_size = 0u;
    uint32_t transformed_size = 0u;
    uint32_t decoded_size = 0u;
    uint32_t restored_size = 0u;
    uint32_t dict_count = 0u;
    uint8_t transform = archive_transform(input, input_size);
    const struct numloss_text_dict_entry *dict = text_dictionary_for_transform(transform, &dict_count);
    int rc = NUMLOSS_OK;

    if (!input || !output || !output_size) return NUMLOSS_ERR_ARGS;

    rc = numloss_read_header(input, input_size, &original_size, &payload_size);
    if (rc != NUMLOSS_OK) return rc;

    if (archive_version(input, input_size) != NUMLOSS_VERSION_V4) return NUMLOSS_ERR_FORMAT;
    if (!dict || payload_size < 4u || input_size != NUMLOSS_HEADER_SIZE + payload_size) {
        return NUMLOSS_ERR_FORMAT;
    }
    if (original_size > output_cap) return NUMLOSS_ERR_OUTPUT;

    transformed_size = read_u32_le(input + NUMLOSS_HEADER_SIZE);
    if (transformed_size > sizeof(g_transform_buf)) return NUMLOSS_ERR_OUTPUT;

    rc = numloss_decode_match_stream(input + NUMLOSS_HEADER_SIZE + 4u, payload_size - 4u,
                                     g_transform_buf, transformed_size, &decoded_size);
    if (rc != NUMLOSS_OK || decoded_size != transformed_size) return NUMLOSS_ERR_FORMAT;

    rc = inverse_text_dictionary_transform(g_transform_buf, transformed_size,
                                           dict, dict_count,
                                           output, output_cap, &restored_size);
    if (rc != NUMLOSS_OK || restored_size != original_size) return NUMLOSS_ERR_FORMAT;

    *output_size = restored_size;
    return NUMLOSS_OK;
}

int numloss_decode(const uint8_t *input, uint32_t input_size,
                   uint8_t *output, uint32_t output_cap,
                   uint32_t *output_size) {
    uint8_t version = archive_version(input, input_size);
    uint32_t original_size = 0;
    uint32_t out_pos = 0;
    int rc = NUMLOSS_OK;

    if (!input || !output || !output_size) return NUMLOSS_ERR_ARGS;

    if (version == NUMLOSS_VERSION_V1) {
        return numloss_decode_v1(input, input_size, output, output_cap, output_size);
    }
    if (version == NUMLOSS_VERSION_V3) {
        return numloss_decode_v3(input, input_size, output, output_cap, output_size);
    }
    if (version == NUMLOSS_VERSION_V4) {
        return numloss_decode_v4(input, input_size, output, output_cap, output_size);
    }
    if (version != NUMLOSS_VERSION_V2) {
        return NUMLOSS_ERR_FORMAT;
    }

    rc = numloss_read_header(input, input_size, &original_size, 0);
    if (rc != NUMLOSS_OK) return rc;
    if (original_size > output_cap) return NUMLOSS_ERR_OUTPUT;

    for (uint32_t in_pos = NUMLOSS_HEADER_SIZE; in_pos < input_size;) {
        uint32_t chunk_original = 0;
        uint32_t chunk_payload = 0;
        uint32_t chunk_size = 0;
        uint32_t chunk_out = 0;

        if (input_size - in_pos < NUMLOSS_HEADER_SIZE) return NUMLOSS_ERR_FORMAT;
        uint8_t chunk_version = archive_version(input + in_pos, input_size - in_pos);

        if (chunk_version != NUMLOSS_VERSION_V1 &&
            chunk_version != NUMLOSS_VERSION_V3 &&
            chunk_version != NUMLOSS_VERSION_V4) {
            return NUMLOSS_ERR_FORMAT;
        }

        rc = numloss_read_header(input + in_pos, input_size - in_pos,
                                 &chunk_original, &chunk_payload);
        if (rc != NUMLOSS_OK) return rc;

        chunk_size = NUMLOSS_HEADER_SIZE + chunk_payload;
        if (chunk_size > input_size - in_pos) return NUMLOSS_ERR_FORMAT;
        if (out_pos + chunk_original > original_size ||
            out_pos + chunk_original > output_cap) {
            return NUMLOSS_ERR_OUTPUT;
        }

        if (chunk_version == NUMLOSS_VERSION_V1) {
            rc = numloss_decode_v1(input + in_pos, chunk_size,
                                   output + out_pos, output_cap - out_pos,
                                   &chunk_out);
        } else if (chunk_version == NUMLOSS_VERSION_V3) {
            rc = numloss_decode_v3(input + in_pos, chunk_size,
                                   output + out_pos, output_cap - out_pos,
                                   &chunk_out);
        } else {
            rc = numloss_decode_v4(input + in_pos, chunk_size,
                                   output + out_pos, output_cap - out_pos,
                                   &chunk_out);
        }
        if (rc != NUMLOSS_OK) return rc;
        if (chunk_out != chunk_original) return NUMLOSS_ERR_FORMAT;

        out_pos += chunk_out;
        in_pos += chunk_size;
    }

    if (out_pos != original_size) return NUMLOSS_ERR_FORMAT;
    *output_size = out_pos;
    return NUMLOSS_OK;
}
