#include "codec.h"

#define NUMLOSS_MAGIC_0 'N'
#define NUMLOSS_MAGIC_1 'M'
#define NUMLOSS_MAGIC_2 'L'
#define NUMLOSS_MAGIC_3 'S'
#define NUMLOSS_LITERAL_MAX 64u
#define NUMLOSS_RUN_MIN 4u
#define NUMLOSS_RUN_MAX 66u
#define NUMLOSS_MATCH_MIN 4u
#define NUMLOSS_MATCH_MAX 66u
#define NUMLOSS_OFFSET_MAX 65535u

#define NUMLOSS_RUN_MIN_V3 3u
#define NUMLOSS_RUN_MAX_V3 66u
#define NUMLOSS_MATCH_MIN_V3 4u
#define NUMLOSS_MATCH_MAX_V3 258u

#define NUMLOSS_SHORT_MATCH_MIN 4u
#define NUMLOSS_SHORT_MATCH_MAX 7u
#define NUMLOSS_SHORT_MATCH_OFFSET_MAX 6144u
#define NUMLOSS_SHORT_MATCH_RANGE NUMLOSS_SHORT_MATCH_OFFSET_MAX
#define NUMLOSS_SHORT_MATCH_TOKEN_BASE 0x80u
#define NUMLOSS_SHORT_MATCH_TOKEN_LAST 0xdfu

#define NUMLOSS_REPEAT_MATCH_MIN 3u
#define NUMLOSS_REPEAT_MATCH_MAX 33u
#define NUMLOSS_REPEAT_MATCH_TOKEN_BASE 0xe0u
#define NUMLOSS_REPEAT_MATCH_TOKEN_LAST 0xfeu
#define NUMLOSS_LONG_MATCH_TOKEN 0xffu
#define NUMLOSS_VISIT_BITMAP_BYTES ((NUMLOSS_MAX_INPUT_BYTES + 7u) / 8u)
#define NUMLOSS_TEXT_ESCAPE 0u
#define NUMLOSS_TEXT_TOKEN_BASE 0x80u

#define NUMLOSS_HASH_BITS 13u
#define NUMLOSS_HASH_SIZE (1u << NUMLOSS_HASH_BITS)
#define NUMLOSS_HASH_WAYS 8u
#define NUMLOSS_INVALID_POS 0xffffffffu

static uint32_t g_history[NUMLOSS_HASH_SIZE * NUMLOSS_HASH_WAYS];
static uint8_t g_transform_buf[NUMLOSS_MAX_INPUT_BYTES];
static uint8_t g_candidate_buf[NUMLOSS_MAX_ARCHIVE_BYTES];
static uint8_t g_visit_bitmap[NUMLOSS_VISIT_BITMAP_BYTES];

enum numloss_choice_kind {
    NUMLOSS_CHOICE_LITERAL = 0,
    NUMLOSS_CHOICE_RUN = 1,
    NUMLOSS_CHOICE_MATCH = 2,
    NUMLOSS_CHOICE_SHORT_MATCH = 3,
    NUMLOSS_CHOICE_REPEAT_MATCH = 4,
    NUMLOSS_CHOICE_LONG_MATCH = 5
};

struct numloss_choice {
    uint32_t kind;
    uint32_t len;
    uint32_t offset;
    uint32_t cost;
    uint32_t gain;
};

struct numloss_match_candidates {
    uint32_t long_len;
    uint32_t long_offset;
    uint32_t short_len;
    uint32_t short_offset;
};

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

static const uint8_t g_transform_candidates[] = {
    NUMLOSS_TRANSFORM_RAW,
    NUMLOSS_TRANSFORM_DELTA8,
    NUMLOSS_TRANSFORM_XOR8,
    NUMLOSS_TRANSFORM_GROUP4,
    NUMLOSS_TRANSFORM_GROUP4_DELTA8,
    NUMLOSS_TRANSFORM_GROUP4_XOR8,
    NUMLOSS_TRANSFORM_GROUP8,
    NUMLOSS_TRANSFORM_GROUP8_DELTA8,
    NUMLOSS_TRANSFORM_GROUP8_XOR8,
    /* --- additions from the time-series and ZipNN papers --- */
    NUMLOSS_TRANSFORM_GROUP2,
    NUMLOSS_TRANSFORM_GROUP2_DELTA8,
    NUMLOSS_TRANSFORM_GROUP2_XOR8,
    NUMLOSS_TRANSFORM_DELTA8_DELTA8
};

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static void write_u32_le(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
    out[2] = (uint8_t)((value >> 16) & 0xffu);
    out[3] = (uint8_t)((value >> 24) & 0xffu);
}

static uint32_t read_u32_le(const uint8_t *in) {
    return (uint32_t)in[0] |
           ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

static uint8_t archive_version(const uint8_t *input, uint32_t input_size) {
    if (!input || input_size < NUMLOSS_HEADER_SIZE) return 0u;
    if (input[0] != NUMLOSS_MAGIC_0 ||
        input[1] != NUMLOSS_MAGIC_1 ||
        input[2] != NUMLOSS_MAGIC_2 ||
        input[3] != NUMLOSS_MAGIC_3) {
        return 0u;
    }
    return input[4];
}

static uint8_t archive_transform(const uint8_t *input, uint32_t input_size) {
    uint8_t version = archive_version(input, input_size);

    if (version != NUMLOSS_VERSION_V3 && version != NUMLOSS_VERSION_V4) {
        return NUMLOSS_TRANSFORM_RAW;
    }
    return input[5];
}

static void write_header(uint8_t *out, uint8_t version,
                         uint8_t flags,
                         uint32_t original_size, uint32_t payload_or_chunk) {
    out[0] = NUMLOSS_MAGIC_0;
    out[1] = NUMLOSS_MAGIC_1;
    out[2] = NUMLOSS_MAGIC_2;
    out[3] = NUMLOSS_MAGIC_3;
    out[4] = version;
    out[5] = flags;
    out[6] = 0;
    out[7] = 0;
    write_u32_le(out + 8, original_size);
    write_u32_le(out + 12, payload_or_chunk);
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
        /*
         * Only validate that the declared payload fits within the supplied
         * buffer when the caller has provided more than the 16-byte header.
         * When cmd_decompress calls us with sizeof(header) == NUMLOSS_HEADER_SIZE
         * we are just parsing header fields; the payload has not been read yet.
         */
        if (input_size > NUMLOSS_HEADER_SIZE &&
            payload > input_size - NUMLOSS_HEADER_SIZE) {
            return NUMLOSS_ERR_FORMAT;
        }
    } else if (version == NUMLOSS_VERSION_V2) {
        payload = input_size - NUMLOSS_HEADER_SIZE;
    } else {
        return NUMLOSS_ERR_FORMAT;
    }

    if (original_size) *original_size = read_u32_le(input + 8);
    if (payload_size) *payload_size = payload;
    return NUMLOSS_OK;
}

static int looks_text_like(const uint8_t *input, uint32_t input_size) {
    uint32_t printable = 0u;

    if (!input || input_size == 0u) return 0;

    for (uint32_t i = 0; i < input_size; i++) {
        uint8_t value = input[i];
        if (value == '\t' || value == '\n' || value == '\r' ||
            (value >= 32u && value < 127u)) {
            printable++;
        }
    }

    return printable * 8u >= input_size * 7u;
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

static void history_reset(void) {
    for (uint32_t i = 0; i < NUMLOSS_HASH_SIZE * NUMLOSS_HASH_WAYS; i++) {
        g_history[i] = NUMLOSS_INVALID_POS;
    }
}

static uint32_t hash4(const uint8_t *data) {
    uint32_t value = ((uint32_t)data[0] << 24) ^
                     ((uint32_t)data[1] << 16) ^
                     ((uint32_t)data[2] << 8) ^
                     (uint32_t)data[3];
    value *= 2654435761u;
    return (value >> (32u - NUMLOSS_HASH_BITS)) & (NUMLOSS_HASH_SIZE - 1u);
}

static void history_insert(const uint8_t *input, uint32_t input_size, uint32_t pos) {
    uint32_t hash = 0;
    uint32_t *slot = 0;

    if (pos + 3u >= input_size) return;

    hash = hash4(input + pos);
    slot = &g_history[hash * NUMLOSS_HASH_WAYS];

    for (uint32_t i = NUMLOSS_HASH_WAYS - 1u; i > 0u; i--) {
        slot[i] = slot[i - 1u];
    }
    slot[0] = pos;
}

static uint32_t find_run_len(const uint8_t *input, uint32_t input_size, uint32_t pos,
                             uint32_t run_max) {
    uint32_t len = 1u;
    uint32_t limit = min_u32(run_max, input_size - pos);
    uint8_t value = input[pos];

    while (len < limit && input[pos + len] == value) len++;
    return len;
}

static uint32_t find_match_len_v1(const uint8_t *input, uint32_t input_size, uint32_t pos,
                                  uint32_t *offset_out) {
    uint32_t hash = 0;
    uint32_t *slot = 0;
    uint32_t best_len = 0;
    uint32_t best_offset = 0;
    uint32_t limit = 0;

    if (pos + NUMLOSS_MATCH_MIN > input_size) return 0;

    hash = hash4(input + pos);
    slot = &g_history[hash * NUMLOSS_HASH_WAYS];
    limit = min_u32(NUMLOSS_MATCH_MAX, input_size - pos);

    for (uint32_t i = 0; i < NUMLOSS_HASH_WAYS; i++) {
        uint32_t candidate = slot[i];
        uint32_t offset = 0;
        uint32_t len = NUMLOSS_MATCH_MIN;

        if (candidate == NUMLOSS_INVALID_POS || candidate >= pos) continue;

        offset = pos - candidate;
        if (offset == 0u || offset > NUMLOSS_OFFSET_MAX) continue;

        if (input[candidate] != input[pos] ||
            input[candidate + 1u] != input[pos + 1u] ||
            input[candidate + 2u] != input[pos + 2u] ||
            input[candidate + 3u] != input[pos + 3u]) continue;

        while (len < limit && input[candidate + len] == input[pos + len]) len++;

        if (len > best_len) {
            best_len = len;
            best_offset = offset;
            if (len == limit) break;
        }
    }

    if (offset_out) *offset_out = best_offset;
    return best_len;
}

static void find_match_candidates_v3(const uint8_t *input, uint32_t input_size, uint32_t pos,
                                     struct numloss_match_candidates *out) {
    uint32_t hash = 0;
    uint32_t *slot = 0;
    uint32_t limit = 0;

    if (!out) return;
    out->long_len = 0u;
    out->long_offset = 0u;
    out->short_len = 0u;
    out->short_offset = 0u;

    if (pos + NUMLOSS_MATCH_MIN_V3 > input_size) return;

    hash = hash4(input + pos);
    slot = &g_history[hash * NUMLOSS_HASH_WAYS];
    limit = min_u32(NUMLOSS_MATCH_MAX_V3, input_size - pos);

    for (uint32_t i = 0; i < NUMLOSS_HASH_WAYS; i++) {
        uint32_t candidate = slot[i];
        uint32_t offset = 0;
        uint32_t len = NUMLOSS_MATCH_MIN_V3;

        if (candidate == NUMLOSS_INVALID_POS || candidate >= pos) continue;

        offset = pos - candidate;
        if (offset == 0u || offset > NUMLOSS_OFFSET_MAX) continue;

        if (input[candidate] != input[pos] ||
            input[candidate + 1u] != input[pos + 1u] ||
            input[candidate + 2u] != input[pos + 2u] ||
            input[candidate + 3u] != input[pos + 3u]) continue;

        while (len < limit && input[candidate + len] == input[pos + len]) len++;

        if (len > out->long_len) {
            out->long_len = len;
            out->long_offset = offset;
        }

        if (offset <= NUMLOSS_SHORT_MATCH_OFFSET_MAX) {
            uint32_t short_len = min_u32(len, NUMLOSS_SHORT_MATCH_MAX);
            if (short_len > out->short_len) {
                out->short_len = short_len;
                out->short_offset = offset;
            }
        }

        if (out->long_len == limit && out->short_len == NUMLOSS_SHORT_MATCH_MAX) break;
    }
}

static uint32_t find_repeat_match_len(const uint8_t *input, uint32_t input_size,
                                      uint32_t pos, uint32_t last_offset) {
    uint32_t len = 0u;
    uint32_t limit = 0;
    uint32_t source = 0;

    if (last_offset == 0u || last_offset > pos) return 0u;

    limit = min_u32(NUMLOSS_REPEAT_MATCH_MAX, input_size - pos);
    source = pos - last_offset;

    while (len < limit && input[source + len] == input[pos + len]) len++;
    return len;
}

static void pick_better_choice(struct numloss_choice *best,
                               uint32_t kind, uint32_t len, uint32_t offset,
                               uint32_t cost, uint32_t gain) {
    if (!best) return;

    if (gain > best->gain ||
        (gain == best->gain && cost < best->cost) ||
        (gain == best->gain && cost == best->cost && len > best->len)) {
        best->kind = kind;
        best->len = len;
        best->offset = offset;
        best->cost = cost;
        best->gain = gain;
    }
}

static void choose_sequence_v1(const uint8_t *input, uint32_t input_size, uint32_t pos,
                               struct numloss_choice *choice) {
    uint32_t run_len = 0;
    uint32_t match_offset = 0;
    uint32_t match_len = 0;

    choice->kind = NUMLOSS_CHOICE_LITERAL;
    choice->len = 1u;
    choice->offset = 0u;
    choice->cost = 2u;
    choice->gain = 0u;

    run_len = find_run_len(input, input_size, pos, NUMLOSS_RUN_MAX);
    match_len = find_match_len_v1(input, input_size, pos, &match_offset);

    if (run_len >= NUMLOSS_RUN_MIN && run_len >= match_len + 1u) {
        choice->kind = NUMLOSS_CHOICE_RUN;
        choice->len = run_len;
        choice->cost = 2u;
        choice->gain = run_len - 2u;
        return;
    }

    if (match_len >= NUMLOSS_MATCH_MIN) {
        choice->kind = NUMLOSS_CHOICE_MATCH;
        choice->len = match_len;
        choice->offset = match_offset;
        choice->cost = 3u;
        choice->gain = match_len - 3u;
    }
}

static void choose_sequence_v3(const uint8_t *input, uint32_t input_size, uint32_t pos,
                               uint32_t last_offset,
                               struct numloss_choice *choice) {
    uint32_t run_len = 0;
    uint32_t repeat_len = 0;
    struct numloss_match_candidates matches;

    choice->kind = NUMLOSS_CHOICE_LITERAL;
    choice->len = 1u;
    choice->offset = 0u;
    choice->cost = 2u;
    choice->gain = 0u;

    run_len = find_run_len(input, input_size, pos, NUMLOSS_RUN_MAX_V3);
    if (run_len >= NUMLOSS_RUN_MIN_V3) {
        pick_better_choice(choice, NUMLOSS_CHOICE_RUN, run_len, 0u, 2u, run_len - 2u);
    }

    find_match_candidates_v3(input, input_size, pos, &matches);

    if (matches.short_len >= NUMLOSS_SHORT_MATCH_MIN) {
        for (uint32_t len = NUMLOSS_SHORT_MATCH_MIN; len <= matches.short_len; len++) {
            pick_better_choice(choice, NUMLOSS_CHOICE_SHORT_MATCH,
                               len, matches.short_offset, 2u, len - 2u);
        }
    }

    if (matches.long_len >= NUMLOSS_MATCH_MIN_V3) {
        pick_better_choice(choice, NUMLOSS_CHOICE_LONG_MATCH,
                           matches.long_len, matches.long_offset,
                           4u, matches.long_len - 4u);
    }

    repeat_len = find_repeat_match_len(input, input_size, pos, last_offset);
    if (repeat_len >= NUMLOSS_REPEAT_MATCH_MIN) {
        pick_better_choice(choice, NUMLOSS_CHOICE_REPEAT_MATCH,
                           repeat_len, last_offset, 1u, repeat_len - 1u);
    }
}

static int emit_literals(uint8_t *output, uint32_t output_cap, uint32_t *output_size,
                         const uint8_t *data, uint32_t len) {
    while (len > 0u) {
        uint32_t chunk = min_u32(len, NUMLOSS_LITERAL_MAX);
        uint32_t pos = *output_size;

        if (pos + 1u + chunk > output_cap) return NUMLOSS_ERR_OUTPUT;

        output[pos++] = (uint8_t)(chunk - 1u);
        memcpy(output + pos, data, chunk);
        pos += chunk;

        *output_size = pos;
        data += chunk;
        len -= chunk;
    }

    return NUMLOSS_OK;
}

static int emit_run(uint8_t *output, uint32_t output_cap, uint32_t *output_size,
                    uint8_t value, uint32_t len, uint32_t run_min) {
    uint32_t pos = *output_size;

    if (pos + 2u > output_cap) return NUMLOSS_ERR_OUTPUT;
    output[pos++] = (uint8_t)(0x40u | (len - run_min));
    output[pos++] = value;
    *output_size = pos;
    return NUMLOSS_OK;
}

static int emit_match(uint8_t *output, uint32_t output_cap, uint32_t *output_size,
                      uint32_t offset, uint32_t len) {
    uint32_t pos = *output_size;

    if (pos + 3u > output_cap) return NUMLOSS_ERR_OUTPUT;
    output[pos++] = (uint8_t)(0x80u | (len - 3u));
    output[pos++] = (uint8_t)(offset & 0xffu);
    output[pos++] = (uint8_t)((offset >> 8) & 0xffu);
    *output_size = pos;
    return NUMLOSS_OK;
}

static int emit_short_match_v3(uint8_t *output, uint32_t output_cap, uint32_t *output_size,
                               uint32_t offset, uint32_t len) {
    uint32_t pos = *output_size;
    uint32_t code = (len - NUMLOSS_SHORT_MATCH_MIN) * NUMLOSS_SHORT_MATCH_RANGE + (offset - 1u);

    if (pos + 2u > output_cap) return NUMLOSS_ERR_OUTPUT;
    output[pos++] = (uint8_t)(NUMLOSS_SHORT_MATCH_TOKEN_BASE + (code >> 8));
    output[pos++] = (uint8_t)(code & 0xffu);
    *output_size = pos;
    return NUMLOSS_OK;
}

static int emit_repeat_match_v3(uint8_t *output, uint32_t output_cap, uint32_t *output_size,
                                uint32_t len) {
    uint32_t pos = *output_size;

    if (pos + 1u > output_cap) return NUMLOSS_ERR_OUTPUT;
    output[pos++] = (uint8_t)(NUMLOSS_REPEAT_MATCH_TOKEN_BASE + (len - NUMLOSS_REPEAT_MATCH_MIN));
    *output_size = pos;
    return NUMLOSS_OK;
}

static int emit_long_match_v3(uint8_t *output, uint32_t output_cap, uint32_t *output_size,
                              uint32_t offset, uint32_t len) {
    uint32_t pos = *output_size;

    if (pos + 4u > output_cap) return NUMLOSS_ERR_OUTPUT;
    output[pos++] = NUMLOSS_LONG_MATCH_TOKEN;
    output[pos++] = (uint8_t)(len - NUMLOSS_MATCH_MIN_V3);
    output[pos++] = (uint8_t)(offset & 0xffu);
    output[pos++] = (uint8_t)((offset >> 8) & 0xffu);
    *output_size = pos;
    return NUMLOSS_OK;
}

static void apply_group_transform(const uint8_t *input, uint32_t input_size,
                                  uint8_t *output, uint32_t width) {
    uint32_t full_words = input_size / width;
    uint32_t out_pos = 0u;

    for (uint32_t lane = 0; lane < width; lane++) {
        for (uint32_t index = 0; index < full_words; index++) {
            output[out_pos++] = input[index * width + lane];
        }
    }

    for (uint32_t index = full_words * width; index < input_size; index++) {
        output[out_pos++] = input[index];
    }
}

static void apply_delta_transform(const uint8_t *input, uint32_t input_size, uint8_t *output) {
    uint8_t prev = 0u;

    for (uint32_t index = 0; index < input_size; index++) {
        uint8_t value = input[index];
        output[index] = (uint8_t)(value - prev);
        prev = value;
    }
}

static void apply_xor_transform(const uint8_t *input, uint32_t input_size, uint8_t *output) {
    uint8_t prev = 0u;

    for (uint32_t index = 0; index < input_size; index++) {
        uint8_t value = input[index];
        output[index] = (uint8_t)(value ^ prev);
        prev = value;
    }
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

/*
 * apply_delta2_transform - second-order (delta-of-delta) coding.
 *
 * First we compute the first-order residuals r[i] = x[i] - x[i-1], then
 * we compute the second-order residuals d[i] = r[i] - r[i-1].  This is
 * most effective when the signal accelerates (or decelerates) smoothly,
 * e.g. ramp signals, because the second-order residuals cluster near zero
 * much more tightly than the raw deltas do.
 *
 * Reference: Matt et al., "Lossless Compression of Time Series Data",
 * arXiv:2510.07015, §III-A (delta coding ablation, "Sine" test case).
 */
static void apply_delta2_transform(const uint8_t *input, uint32_t input_size,
                                   uint8_t *output) {
    uint8_t prev  = 0u;
    uint8_t delta = 0u;

    for (uint32_t index = 0; index < input_size; index++) {
        uint8_t value     = input[index];
        uint8_t new_delta = (uint8_t)(value - prev);
        output[index]     = (uint8_t)(new_delta - delta);
        prev              = value;
        delta             = new_delta;
    }
}

static void inverse_delta2_in_place(uint8_t *data, uint32_t input_size) {
    uint8_t delta = 0u;
    uint8_t value = 0u;

    for (uint32_t index = 0; index < input_size; index++) {
        delta = (uint8_t)(delta + data[index]);   /* un-second-delta  */
        value = (uint8_t)(value + delta);          /* un-first-delta   */
        data[index] = value;
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

static int apply_text_dictionary_transform(const uint8_t *input, uint32_t input_size,
                                           const struct numloss_text_dict_entry *dict,
                                           uint32_t dict_count,
                                           uint8_t *output, uint32_t output_cap,
                                           uint32_t *output_size) {
    uint32_t in_pos = 0u;
    uint32_t out_pos = 0u;

    if (!input || !dict || !output || !output_size) return NUMLOSS_ERR_ARGS;
    if (!looks_text_like(input, input_size)) return NUMLOSS_ERR_FORMAT;

    while (in_pos < input_size) {
        uint32_t best_index = 0u;
        uint32_t best_len = 0u;

        for (uint32_t i = 0; i < dict_count; i++) {
            uint32_t len = dict[i].len;

            if (len <= best_len || in_pos + len > input_size) continue;
            if (memcmp(input + in_pos, dict[i].text, len) != 0) continue;

            best_len = len;
            best_index = i;
        }

        if (best_len > 0u) {
            if (best_index >= 128u || out_pos + 1u > output_cap) return NUMLOSS_ERR_OUTPUT;
            output[out_pos++] = (uint8_t)(NUMLOSS_TEXT_TOKEN_BASE + best_index);
            in_pos += best_len;
            continue;
        }

        if (input[in_pos] == NUMLOSS_TEXT_ESCAPE || input[in_pos] > 0x7fu) {
            if (out_pos + 2u > output_cap) return NUMLOSS_ERR_OUTPUT;
            output[out_pos++] = NUMLOSS_TEXT_ESCAPE;
            output[out_pos++] = input[in_pos++];
            continue;
        }

        if (out_pos + 1u > output_cap) return NUMLOSS_ERR_OUTPUT;
        output[out_pos++] = input[in_pos++];
    }

    *output_size = out_pos;
    return NUMLOSS_OK;
}

static int inverse_text_dictionary_transform(const uint8_t *input, uint32_t input_size,
                                             const struct numloss_text_dict_entry *dict,
                                             uint32_t dict_count,
                                             uint8_t *output, uint32_t output_cap,
                                             uint32_t *output_size) {
    uint32_t in_pos = 0u;
    uint32_t out_pos = 0u;

    if (!input || !dict || !output || !output_size) return NUMLOSS_ERR_ARGS;

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

    *output_size = out_pos;
    return NUMLOSS_OK;
}

static int apply_transform(const uint8_t *input, uint32_t input_size,
                           uint8_t transform,
                           const uint8_t **encoded_input_out) {
    if (!encoded_input_out) return NUMLOSS_ERR_ARGS;

    if (transform == NUMLOSS_TRANSFORM_RAW) {
        *encoded_input_out = input;
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_DELTA8) {
        apply_delta_transform(input, input_size, g_transform_buf);
        *encoded_input_out = g_transform_buf;
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_XOR8) {
        apply_xor_transform(input, input_size, g_transform_buf);
        *encoded_input_out = g_transform_buf;
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP4) {
        apply_group_transform(input, input_size, g_transform_buf, 4u);
        *encoded_input_out = g_transform_buf;
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP4_DELTA8) {
        apply_group_transform(input, input_size, g_transform_buf, 4u);
        apply_delta_transform(g_transform_buf, input_size, g_transform_buf);
        *encoded_input_out = g_transform_buf;
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP4_XOR8) {
        apply_group_transform(input, input_size, g_transform_buf, 4u);
        apply_xor_transform(g_transform_buf, input_size, g_transform_buf);
        *encoded_input_out = g_transform_buf;
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP8) {
        apply_group_transform(input, input_size, g_transform_buf, 8u);
        *encoded_input_out = g_transform_buf;
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP8_DELTA8) {
        apply_group_transform(input, input_size, g_transform_buf, 8u);
        apply_delta_transform(g_transform_buf, input_size, g_transform_buf);
        *encoded_input_out = g_transform_buf;
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP8_XOR8) {
        apply_group_transform(input, input_size, g_transform_buf, 8u);
        apply_xor_transform(g_transform_buf, input_size, g_transform_buf);
        *encoded_input_out = g_transform_buf;
        return NUMLOSS_OK;
    }

    /* --- transforms added from the time-series and ZipNN papers --- */

    if (transform == NUMLOSS_TRANSFORM_GROUP2) {
        apply_group_transform(input, input_size, g_transform_buf, 2u);
        *encoded_input_out = g_transform_buf;
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP2_DELTA8) {
        apply_group_transform(input, input_size, g_transform_buf, 2u);
        apply_delta_transform(g_transform_buf, input_size, g_transform_buf);
        *encoded_input_out = g_transform_buf;
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_GROUP2_XOR8) {
        apply_group_transform(input, input_size, g_transform_buf, 2u);
        apply_xor_transform(g_transform_buf, input_size, g_transform_buf);
        *encoded_input_out = g_transform_buf;
        return NUMLOSS_OK;
    }

    if (transform == NUMLOSS_TRANSFORM_DELTA8_DELTA8) {
        apply_delta2_transform(input, input_size, g_transform_buf);
        *encoded_input_out = g_transform_buf;
        return NUMLOSS_OK;
    }

    return NUMLOSS_ERR_FORMAT;
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

    /* --- new transforms --- */

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
            if (out_pos + len > original_size || out_pos + len > output_cap) return NUMLOSS_ERR_OUTPUT;
            memcpy(output + out_pos, input + in_pos, len);
            in_pos += len;
            out_pos += len;
            continue;
        }

        if (kind == 1u) {
            uint8_t value = 0;

            len = (uint32_t)(token & 0x3fu) + 3u;
            if (in_pos >= in_end) return NUMLOSS_ERR_FORMAT;
            if (out_pos + len > original_size || out_pos + len > output_cap) return NUMLOSS_ERR_OUTPUT;

            value = input[in_pos++];
            memset(output + out_pos, value, len);
            out_pos += len;
            continue;
        }

        if (kind == 2u) {
            uint32_t offset = 0;

            len = (uint32_t)(token & 0x3fu) + 3u;
            if (in_pos + 2u > in_end) return NUMLOSS_ERR_FORMAT;

            offset = (uint32_t)input[in_pos] | ((uint32_t)input[in_pos + 1u] << 8);
            in_pos += 2u;

            if (offset == 0u || offset > out_pos) return NUMLOSS_ERR_FORMAT;
            if (out_pos + len > original_size || out_pos + len > output_cap) return NUMLOSS_ERR_OUTPUT;

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
            uint32_t offset = (uint32_t)payload[in_pos] | ((uint32_t)payload[in_pos + 1u] << 8);
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

    if (output_size) *output_size = out_pos;
    return NUMLOSS_OK;
}

static int numloss_decode_v3(const uint8_t *input, uint32_t input_size,
                             uint8_t *output, uint32_t output_cap,
                             uint32_t *output_size) {
    uint32_t original_size = 0;
    uint32_t payload_size = 0;
    uint8_t transform = archive_transform(input, input_size);
    int rc = NUMLOSS_OK;

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

    rc = numloss_read_header(input, input_size, &original_size, &payload_size);
    if (rc != NUMLOSS_OK) return rc;

    if (archive_version(input, input_size) != NUMLOSS_VERSION_V4) return NUMLOSS_ERR_FORMAT;
    if (!dict || payload_size < 4u || input_size != NUMLOSS_HEADER_SIZE + payload_size) {
        return NUMLOSS_ERR_FORMAT;
    }
    if (original_size > output_cap) return NUMLOSS_ERR_OUTPUT;

    transformed_size = read_u32_le(input + NUMLOSS_HEADER_SIZE);
    if (transformed_size > NUMLOSS_MAX_INPUT_BYTES) return NUMLOSS_ERR_OUTPUT;

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

static int numloss_encode_v1(const uint8_t *input, uint32_t input_size,
                             uint8_t *output, uint32_t output_cap,
                             uint32_t *output_size) {
    uint32_t pos = 0;
    uint32_t out_pos = NUMLOSS_HEADER_SIZE;
    uint32_t literal_start = 0;
    uint32_t literal_len = 0;
    int rc = NUMLOSS_OK;

    if (output_cap < NUMLOSS_HEADER_SIZE) return NUMLOSS_ERR_OUTPUT;

    history_reset();
    write_header(output, NUMLOSS_VERSION_V1, 0u, input_size, 0u);

    while (pos < input_size) {
        struct numloss_choice choice;

        choose_sequence_v1(input, input_size, pos, &choice);

        if (choice.kind == NUMLOSS_CHOICE_RUN) {
            if (literal_len > 0u) {
                rc = emit_literals(output, output_cap, &out_pos, input + literal_start, literal_len);
                if (rc != NUMLOSS_OK) return rc;
                literal_len = 0u;
            }

            rc = emit_run(output, output_cap, &out_pos, input[pos], choice.len, NUMLOSS_RUN_MIN);
            if (rc != NUMLOSS_OK) return rc;

            for (uint32_t i = 0; i < choice.len; i++) history_insert(input, input_size, pos + i);
            pos += choice.len;
            literal_start = pos;
            continue;
        }

        if (choice.kind == NUMLOSS_CHOICE_MATCH) {
            if (literal_len > 0u) {
                rc = emit_literals(output, output_cap, &out_pos, input + literal_start, literal_len);
                if (rc != NUMLOSS_OK) return rc;
                literal_len = 0u;
            }

            rc = emit_match(output, output_cap, &out_pos, choice.offset, choice.len);
            if (rc != NUMLOSS_OK) return rc;

            for (uint32_t i = 0; i < choice.len; i++) history_insert(input, input_size, pos + i);
            pos += choice.len;
            literal_start = pos;
            continue;
        }

        if (literal_len == 0u) literal_start = pos;
        history_insert(input, input_size, pos);
        literal_len++;
        pos++;

        if (literal_len == NUMLOSS_LITERAL_MAX) {
            rc = emit_literals(output, output_cap, &out_pos, input + literal_start, literal_len);
            if (rc != NUMLOSS_OK) return rc;
            literal_len = 0u;
            literal_start = pos;
        }
    }

    if (literal_len > 0u) {
        rc = emit_literals(output, output_cap, &out_pos, input + literal_start, literal_len);
        if (rc != NUMLOSS_OK) return rc;
    }

    write_header(output, NUMLOSS_VERSION_V1, 0u, input_size, out_pos - NUMLOSS_HEADER_SIZE);
    *output_size = out_pos;
    return NUMLOSS_OK;
}

static int numloss_encode_match_stream(const uint8_t *source, uint32_t source_size,
                                       uint32_t original_size,
                                       uint8_t version, uint8_t transform,
                                       const uint8_t *prefix, uint32_t prefix_size,
                                       uint8_t *output, uint32_t output_cap,
                                       uint32_t *output_size) {
    uint32_t pos = 0u;
    uint32_t out_pos = NUMLOSS_HEADER_SIZE + prefix_size;
    uint32_t literal_start = 0;
    uint32_t literal_len = 0;
    uint32_t last_offset = 0;
    int rc = NUMLOSS_OK;

    if (output_cap < out_pos) return NUMLOSS_ERR_OUTPUT;

    history_reset();
    write_header(output, version, transform, original_size, 0u);
    if (prefix_size > 0u && prefix) memcpy(output + NUMLOSS_HEADER_SIZE, prefix, prefix_size);

    while (pos < source_size) {
        struct numloss_choice choice;

        choose_sequence_v3(source, source_size, pos, last_offset, &choice);

        if (choice.kind == NUMLOSS_CHOICE_RUN) {
            if (literal_len > 0u) {
                rc = emit_literals(output, output_cap, &out_pos, source + literal_start, literal_len);
                if (rc != NUMLOSS_OK) return rc;
                literal_len = 0u;
            }

            rc = emit_run(output, output_cap, &out_pos, source[pos], choice.len, NUMLOSS_RUN_MIN_V3);
            if (rc != NUMLOSS_OK) return rc;

            for (uint32_t i = 0; i < choice.len; i++) history_insert(source, source_size, pos + i);
            pos += choice.len;
            literal_start = pos;
            continue;
        }

        if (choice.kind == NUMLOSS_CHOICE_SHORT_MATCH) {
            if (literal_len > 0u) {
                rc = emit_literals(output, output_cap, &out_pos, source + literal_start, literal_len);
                if (rc != NUMLOSS_OK) return rc;
                literal_len = 0u;
            }

            rc = emit_short_match_v3(output, output_cap, &out_pos, choice.offset, choice.len);
            if (rc != NUMLOSS_OK) return rc;

            last_offset = choice.offset;
            for (uint32_t i = 0; i < choice.len; i++) history_insert(source, source_size, pos + i);
            pos += choice.len;
            literal_start = pos;
            continue;
        }

        if (choice.kind == NUMLOSS_CHOICE_REPEAT_MATCH) {
            if (literal_len > 0u) {
                rc = emit_literals(output, output_cap, &out_pos, source + literal_start, literal_len);
                if (rc != NUMLOSS_OK) return rc;
                literal_len = 0u;
            }

            rc = emit_repeat_match_v3(output, output_cap, &out_pos, choice.len);
            if (rc != NUMLOSS_OK) return rc;

            for (uint32_t i = 0; i < choice.len; i++) history_insert(source, source_size, pos + i);
            pos += choice.len;
            literal_start = pos;
            continue;
        }

        if (choice.kind == NUMLOSS_CHOICE_LONG_MATCH) {
            if (literal_len > 0u) {
                rc = emit_literals(output, output_cap, &out_pos, source + literal_start, literal_len);
                if (rc != NUMLOSS_OK) return rc;
                literal_len = 0u;
            }

            rc = emit_long_match_v3(output, output_cap, &out_pos, choice.offset, choice.len);
            if (rc != NUMLOSS_OK) return rc;

            last_offset = choice.offset;
            for (uint32_t i = 0; i < choice.len; i++) history_insert(source, source_size, pos + i);
            pos += choice.len;
            literal_start = pos;
            continue;
        }

        if (literal_len == 0u) literal_start = pos;
        history_insert(source, source_size, pos);
        literal_len++;
        pos++;

        if (literal_len == NUMLOSS_LITERAL_MAX) {
            rc = emit_literals(output, output_cap, &out_pos, source + literal_start, literal_len);
            if (rc != NUMLOSS_OK) return rc;
            literal_len = 0u;
            literal_start = pos;
        }
    }

    if (literal_len > 0u) {
        rc = emit_literals(output, output_cap, &out_pos, source + literal_start, literal_len);
        if (rc != NUMLOSS_OK) return rc;
    }

    write_header(output, version, transform, original_size, out_pos - NUMLOSS_HEADER_SIZE);
    *output_size = out_pos;
    return NUMLOSS_OK;
}

static int numloss_encode_v3(const uint8_t *input, uint32_t input_size,
                             uint8_t transform,
                             uint8_t *output, uint32_t output_cap,
                             uint32_t *output_size) {
    const uint8_t *source = input;
    int rc = apply_transform(input, input_size, transform, &source);

    if (rc != NUMLOSS_OK) return rc;
    return numloss_encode_match_stream(source, input_size, input_size,
                                       NUMLOSS_VERSION_V3, transform,
                                       0, 0u,
                                       output, output_cap, output_size);
}

static int numloss_encode_v4_text(const uint8_t *input, uint32_t input_size,
                                  uint8_t transform,
                                  uint8_t *output, uint32_t output_cap,
                                  uint32_t *output_size) {
    uint32_t dict_count = 0u;
    uint32_t transformed_size = 0u;
    uint8_t prefix[4];
    const struct numloss_text_dict_entry *dict = text_dictionary_for_transform(transform, &dict_count);
    int rc = 0;

    if (!dict) return NUMLOSS_ERR_FORMAT;

    rc = apply_text_dictionary_transform(input, input_size,
                                         dict, dict_count,
                                         g_transform_buf, input_size,
                                         &transformed_size);
    if (rc != NUMLOSS_OK) return rc;

    write_u32_le(prefix, transformed_size);
    return numloss_encode_match_stream(g_transform_buf, transformed_size, input_size,
                                       NUMLOSS_VERSION_V4, transform,
                                       prefix, sizeof(prefix),
                                       output, output_cap, output_size);
}

int numloss_encode(const uint8_t *input, uint32_t input_size,
                   uint8_t *output, uint32_t output_cap,
                   uint32_t *output_size) {
    uint32_t best_size = 0u;
    int rc = NUMLOSS_OK;

    if (!input || !output || !output_size) return NUMLOSS_ERR_ARGS;
    if (input_size > NUMLOSS_MAX_INPUT_BYTES) return NUMLOSS_ERR_INPUT;
    if (output_cap < NUMLOSS_HEADER_SIZE) return NUMLOSS_ERR_OUTPUT;

    rc = numloss_encode_v1(input, input_size, output, output_cap, &best_size);
    if (rc != NUMLOSS_OK) return rc;

    for (uint32_t i = 0; i < sizeof(g_transform_candidates); i++) {
        uint32_t candidate_size = 0u;
        int candidate_rc = numloss_encode_v3(input, input_size,
                                             g_transform_candidates[i],
                                             g_candidate_buf, output_cap,
                                             &candidate_size);

        if (candidate_rc != NUMLOSS_OK) continue;
        if (candidate_size < best_size) {
            memcpy(output, g_candidate_buf, candidate_size);
            best_size = candidate_size;
        }
    }

    for (uint32_t i = 0; i < 2u; i++) {
        uint32_t candidate_size = 0u;
        uint8_t transform = (i == 0u) ? NUMLOSS_TRANSFORM_TEXT_PROSE
                                      : NUMLOSS_TRANSFORM_TEXT_CODE;
        int candidate_rc = numloss_encode_v4_text(input, input_size,
                                                  transform,
                                                  g_candidate_buf, output_cap,
                                                  &candidate_size);

        if (candidate_rc != NUMLOSS_OK) continue;
        if (candidate_size < best_size) {
            memcpy(output, g_candidate_buf, candidate_size);
            best_size = candidate_size;
        }
    }

    *output_size = best_size;
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
