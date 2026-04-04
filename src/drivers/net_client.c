#include "drivers/network.h"

#include "drivers/timer.h"
#include "kernel/kernel.h"

#define TLS_VERSION_1_2             0x0303
#define TLS_RECORD_CHANGE_CIPHER    20
#define TLS_RECORD_ALERT            21
#define TLS_RECORD_HANDSHAKE        22
#define TLS_RECORD_APPLICATION      23

#define TLS_HANDSHAKE_CLIENT_HELLO       1
#define TLS_HANDSHAKE_SERVER_HELLO       2
#define TLS_HANDSHAKE_CERTIFICATE       11
#define TLS_HANDSHAKE_SERVER_HELLO_DONE 14
#define TLS_HANDSHAKE_CLIENT_KEY_EXCH   16
#define TLS_HANDSHAKE_FINISHED          20

#define TLS_ALERT_CLOSE_NOTIFY      0

#define TLS_CIPHER_RSA_AES128_SHA256 0x003C

#define TLS_RANDOM_LEN              32
#define TLS_MASTER_SECRET_LEN       48
#define TLS_VERIFY_DATA_LEN         12
#define TLS_AES_KEY_LEN             16
#define TLS_AES_BLOCK_LEN           16
#define TLS_SHA256_LEN              32
#define TLS_MAC_KEY_LEN             32
#define TLS_KEY_BLOCK_LEN           (2 * TLS_MAC_KEY_LEN + 2 * TLS_AES_KEY_LEN)

#define TLS_MAX_RECORD_LEN          16384
#define TLS_MAX_FRAGMENT_LEN        1200
#define TLS_MAX_CERT_LEN            4096
#define TLS_MAX_TRANSCRIPT_LEN      16384

#define TLS_RSA_ENC_MIN_LEN         64

#define NET_OK                   0
#define NET_ERR_GENERIC         -1
#define NET_ERR_INVALID         -5

struct tls_sha256_ctx {
    uint32_t state[8];
    uint64_t total_len;
    uint8_t  block[64];
    size_t   block_len;
};

struct tls_aes_ctx {
    uint8_t round_keys[176];
};

struct tls_session {
    int      tcp_handle;
    uint16_t protocol_version;
    uint16_t cipher_suite;
    char     server_name[NET_HOST_NAME_LEN];
    uint8_t  client_random[TLS_RANDOM_LEN];
    uint8_t  server_random[TLS_RANDOM_LEN];
    uint8_t  master_secret[TLS_MASTER_SECRET_LEN];
    uint8_t  client_mac_key[TLS_MAC_KEY_LEN];
    uint8_t  server_mac_key[TLS_MAC_KEY_LEN];
    uint8_t  client_key[TLS_AES_KEY_LEN];
    uint8_t  server_key[TLS_AES_KEY_LEN];
    uint64_t write_seq;
    uint64_t read_seq;
    struct tls_aes_ctx client_aes;
    struct tls_aes_ctx server_aes;
};

static const uint32_t g_tls_sha256_k[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

static const uint8_t g_tls_aes_sbox[256] = {
    0x63,0x7C,0x77,0x7B,0xF2,0x6B,0x6F,0xC5,0x30,0x01,0x67,0x2B,0xFE,0xD7,0xAB,0x76,
    0xCA,0x82,0xC9,0x7D,0xFA,0x59,0x47,0xF0,0xAD,0xD4,0xA2,0xAF,0x9C,0xA4,0x72,0xC0,
    0xB7,0xFD,0x93,0x26,0x36,0x3F,0xF7,0xCC,0x34,0xA5,0xE5,0xF1,0x71,0xD8,0x31,0x15,
    0x04,0xC7,0x23,0xC3,0x18,0x96,0x05,0x9A,0x07,0x12,0x80,0xE2,0xEB,0x27,0xB2,0x75,
    0x09,0x83,0x2C,0x1A,0x1B,0x6E,0x5A,0xA0,0x52,0x3B,0xD6,0xB3,0x29,0xE3,0x2F,0x84,
    0x53,0xD1,0x00,0xED,0x20,0xFC,0xB1,0x5B,0x6A,0xCB,0xBE,0x39,0x4A,0x4C,0x58,0xCF,
    0xD0,0xEF,0xAA,0xFB,0x43,0x4D,0x33,0x85,0x45,0xF9,0x02,0x7F,0x50,0x3C,0x9F,0xA8,
    0x51,0xA3,0x40,0x8F,0x92,0x9D,0x38,0xF5,0xBC,0xB6,0xDA,0x21,0x10,0xFF,0xF3,0xD2,
    0xCD,0x0C,0x13,0xEC,0x5F,0x97,0x44,0x17,0xC4,0xA7,0x7E,0x3D,0x64,0x5D,0x19,0x73,
    0x60,0x81,0x4F,0xDC,0x22,0x2A,0x90,0x88,0x46,0xEE,0xB8,0x14,0xDE,0x5E,0x0B,0xDB,
    0xE0,0x32,0x3A,0x0A,0x49,0x06,0x24,0x5C,0xC2,0xD3,0xAC,0x62,0x91,0x95,0xE4,0x79,
    0xE7,0xC8,0x37,0x6D,0x8D,0xD5,0x4E,0xA9,0x6C,0x56,0xF4,0xEA,0x65,0x7A,0xAE,0x08,
    0xBA,0x78,0x25,0x2E,0x1C,0xA6,0xB4,0xC6,0xE8,0xDD,0x74,0x1F,0x4B,0xBD,0x8B,0x8A,
    0x70,0x3E,0xB5,0x66,0x48,0x03,0xF6,0x0E,0x61,0x35,0x57,0xB9,0x86,0xC1,0x1D,0x9E,
    0xE1,0xF8,0x98,0x11,0x69,0xD9,0x8E,0x94,0x9B,0x1E,0x87,0xE9,0xCE,0x55,0x28,0xDF,
    0x8C,0xA1,0x89,0x0D,0xBF,0xE6,0x42,0x68,0x41,0x99,0x2D,0x0F,0xB0,0x54,0xBB,0x16
};

static const uint8_t g_tls_aes_inv_sbox[256] = {
    0x52,0x09,0x6A,0xD5,0x30,0x36,0xA5,0x38,0xBF,0x40,0xA3,0x9E,0x81,0xF3,0xD7,0xFB,
    0x7C,0xE3,0x39,0x82,0x9B,0x2F,0xFF,0x87,0x34,0x8E,0x43,0x44,0xC4,0xDE,0xE9,0xCB,
    0x54,0x7B,0x94,0x32,0xA6,0xC2,0x23,0x3D,0xEE,0x4C,0x95,0x0B,0x42,0xFA,0xC3,0x4E,
    0x08,0x2E,0xA1,0x66,0x28,0xD9,0x24,0xB2,0x76,0x5B,0xA2,0x49,0x6D,0x8B,0xD1,0x25,
    0x72,0xF8,0xF6,0x64,0x86,0x68,0x98,0x16,0xD4,0xA4,0x5C,0xCC,0x5D,0x65,0xB6,0x92,
    0x6C,0x70,0x48,0x50,0xFD,0xED,0xB9,0xDA,0x5E,0x15,0x46,0x57,0xA7,0x8D,0x9D,0x84,
    0x90,0xD8,0xAB,0x00,0x8C,0xBC,0xD3,0x0A,0xF7,0xE4,0x58,0x05,0xB8,0xB3,0x45,0x06,
    0xD0,0x2C,0x1E,0x8F,0xCA,0x3F,0x0F,0x02,0xC1,0xAF,0xBD,0x03,0x01,0x13,0x8A,0x6B,
    0x3A,0x91,0x11,0x41,0x4F,0x67,0xDC,0xEA,0x97,0xF2,0xCF,0xCE,0xF0,0xB4,0xE6,0x73,
    0x96,0xAC,0x74,0x22,0xE7,0xAD,0x35,0x85,0xE2,0xF9,0x37,0xE8,0x1C,0x75,0xDF,0x6E,
    0x47,0xF1,0x1A,0x71,0x1D,0x29,0xC5,0x89,0x6F,0xB7,0x62,0x0E,0xAA,0x18,0xBE,0x1B,
    0xFC,0x56,0x3E,0x4B,0xC6,0xD2,0x79,0x20,0x9A,0xDB,0xC0,0xFE,0x78,0xCD,0x5A,0xF4,
    0x1F,0xDD,0xA8,0x33,0x88,0x07,0xC7,0x31,0xB1,0x12,0x10,0x59,0x27,0x80,0xEC,0x5F,
    0x60,0x51,0x7F,0xA9,0x19,0xB5,0x4A,0x0D,0x2D,0xE5,0x7A,0x9F,0x93,0xC9,0x9C,0xEF,
    0xA0,0xE0,0x3B,0x4D,0xAE,0x2A,0xF5,0xB0,0xC8,0xEB,0xBB,0x3C,0x83,0x53,0x99,0x61,
    0x17,0x2B,0x04,0x7E,0xBA,0x77,0xD6,0x26,0xE1,0x69,0x14,0x63,0x55,0x21,0x0C,0x7D
};

static const uint8_t g_tls_aes_rcon[11] = {
    0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36
};

static uint16_t tls_read_be16(const void *ptr) {
    const uint8_t *p = (const uint8_t *)ptr;
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t tls_read_be24(const void *ptr) {
    const uint8_t *p = (const uint8_t *)ptr;
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static uint32_t tls_read_be32(const void *ptr) {
    const uint8_t *p = (const uint8_t *)ptr;
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3]);
}

static void tls_write_be16(void *ptr, uint16_t value) {
    uint8_t *p = (uint8_t *)ptr;
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)(value & 0xFFu);
}

static void tls_write_be24(void *ptr, uint32_t value) {
    uint8_t *p = (uint8_t *)ptr;
    p[0] = (uint8_t)((value >> 16) & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)(value & 0xFFu);
}

static void tls_write_be32(void *ptr, uint32_t value) {
    uint8_t *p = (uint8_t *)ptr;
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)((value >> 16) & 0xFFu);
    p[2] = (uint8_t)((value >> 8) & 0xFFu);
    p[3] = (uint8_t)(value & 0xFFu);
}

static void tls_write_be64(void *ptr, uint64_t value) {
    uint8_t *p = (uint8_t *)ptr;
    for (int i = 7; i >= 0; i--) {
        p[7 - i] = (uint8_t)((value >> (i * 8)) & 0xFFu);
    }
}

static void tls_copy_string(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int tls_parse_status_code(const char *text) {
    int value = 0;
    int digits = 0;
    while (*text == ' ') text++;
    while (*text >= '0' && *text <= '9' && digits < 3) {
        value = (value * 10) + (*text - '0');
        text++;
        digits++;
    }
    return digits == 3 ? value : 0;
}

static int tls_is_ipv4_text(const char *text) {
    int dots = 0;
    if (!text || !*text) return 0;
    while (*text) {
        if ((*text >= '0' && *text <= '9') || *text == '.') {
            if (*text == '.') dots++;
            text++;
            continue;
        }
        return 0;
    }
    return dots == 3;
}

static void tls_random_bytes(uint8_t *out, size_t len) {
    static uint64_t state = 0x4E554D4F53544C53ull;
    struct net_info info;

    if (!out) return;
    if (net_get_info(&info) == 0) {
        state ^= ((uint64_t)info.mac[0] << 40) |
                 ((uint64_t)info.mac[1] << 32) |
                 ((uint64_t)info.mac[2] << 24) |
                 ((uint64_t)info.mac[3] << 16) |
                 ((uint64_t)info.mac[4] << 8)  |
                 ((uint64_t)info.mac[5]);
    }
    state ^= timer_get_uptime_ms();
    for (size_t i = 0; i < len; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        out[i] = (uint8_t)((state >> ((i & 7u) * 8u)) & 0xFFu);
    }
}

static uint32_t tls_rotr32(uint32_t value, uint32_t shift) {
    return (value >> shift) | (value << (32u - shift));
}

static void tls_sha256_init(struct tls_sha256_ctx *ctx) {
    ctx->state[0] = 0x6A09E667u;
    ctx->state[1] = 0xBB67AE85u;
    ctx->state[2] = 0x3C6EF372u;
    ctx->state[3] = 0xA54FF53Au;
    ctx->state[4] = 0x510E527Fu;
    ctx->state[5] = 0x9B05688Cu;
    ctx->state[6] = 0x1F83D9ABu;
    ctx->state[7] = 0x5BE0CD19u;
    ctx->total_len = 0;
    ctx->block_len = 0;
}

static void tls_sha256_transform(struct tls_sha256_ctx *ctx, const uint8_t block[64]) {
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;

    for (int i = 0; i < 16; i++) {
        w[i] = tls_read_be32(block + (size_t)i * 4u);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = tls_rotr32(w[i - 15], 7) ^ tls_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = tls_rotr32(w[i - 2], 17) ^ tls_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t s1 = tls_rotr32(e, 6) ^ tls_rotr32(e, 11) ^ tls_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + g_tls_sha256_k[i] + w[i];
        uint32_t s0 = tls_rotr32(a, 2) ^ tls_rotr32(a, 13) ^ tls_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void tls_sha256_update(struct tls_sha256_ctx *ctx, const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    if (!ctx || (!bytes && len != 0)) return;

    ctx->total_len += len;
    while (len > 0) {
        size_t space = 64u - ctx->block_len;
        size_t chunk = (len < space) ? len : space;
        memcpy(ctx->block + ctx->block_len, bytes, chunk);
        ctx->block_len += chunk;
        bytes += chunk;
        len -= chunk;

        if (ctx->block_len == 64u) {
            tls_sha256_transform(ctx, ctx->block);
            ctx->block_len = 0;
        }
    }
}

static void tls_sha256_final(struct tls_sha256_ctx *ctx, uint8_t out[32]) {
    uint64_t bit_len = ctx->total_len * 8u;

    ctx->block[ctx->block_len++] = 0x80;
    if (ctx->block_len > 56u) {
        while (ctx->block_len < 64u) ctx->block[ctx->block_len++] = 0;
        tls_sha256_transform(ctx, ctx->block);
        ctx->block_len = 0;
    }
    while (ctx->block_len < 56u) ctx->block[ctx->block_len++] = 0;
    tls_write_be64(ctx->block + 56, bit_len);
    tls_sha256_transform(ctx, ctx->block);

    for (int i = 0; i < 8; i++) {
        tls_write_be32(out + (size_t)i * 4u, ctx->state[i]);
    }
}

static void tls_sha256_digest(const void *data, size_t len, uint8_t out[32]) {
    struct tls_sha256_ctx ctx;
    tls_sha256_init(&ctx);
    tls_sha256_update(&ctx, data, len);
    tls_sha256_final(&ctx, out);
}

static void tls_hmac_sha256(const uint8_t *key, size_t key_len,
                            const void *data, size_t data_len,
                            uint8_t out[32]) {
    uint8_t key_block[64];
    uint8_t ipad[64];
    uint8_t opad[64];
    uint8_t inner[32];
    struct tls_sha256_ctx ctx;

    memset(key_block, 0, sizeof(key_block));
    if (key_len > sizeof(key_block)) {
        tls_sha256_digest(key, key_len, key_block);
    } else if (key && key_len) {
        memcpy(key_block, key, key_len);
    }

    for (size_t i = 0; i < sizeof(key_block); i++) {
        ipad[i] = (uint8_t)(key_block[i] ^ 0x36u);
        opad[i] = (uint8_t)(key_block[i] ^ 0x5Cu);
    }

    tls_sha256_init(&ctx);
    tls_sha256_update(&ctx, ipad, sizeof(ipad));
    tls_sha256_update(&ctx, data, data_len);
    tls_sha256_final(&ctx, inner);

    tls_sha256_init(&ctx);
    tls_sha256_update(&ctx, opad, sizeof(opad));
    tls_sha256_update(&ctx, inner, sizeof(inner));
    tls_sha256_final(&ctx, out);
}

static void tls_prf_sha256(const uint8_t *secret, size_t secret_len,
                           const char *label,
                           const uint8_t *seed1, size_t seed1_len,
                           const uint8_t *seed2, size_t seed2_len,
                           uint8_t *out, size_t out_len) {
    uint8_t a[32];
    uint8_t hmac_input[128];
    uint8_t block[32];
    size_t label_len = strlen(label);
    size_t seed_len = label_len + seed1_len + seed2_len;

    if (seed_len > sizeof(hmac_input)) return;

    memcpy(hmac_input, label, label_len);
    memcpy(hmac_input + label_len, seed1, seed1_len);
    memcpy(hmac_input + label_len + seed1_len, seed2, seed2_len);
    tls_hmac_sha256(secret, secret_len, hmac_input, seed_len, a);

    size_t produced = 0;
    while (produced < out_len) {
        uint8_t buffer[160];
        memcpy(buffer, a, sizeof(a));
        memcpy(buffer + sizeof(a), hmac_input, seed_len);
        tls_hmac_sha256(secret, secret_len, buffer, sizeof(a) + seed_len, block);

        size_t chunk = out_len - produced;
        if (chunk > sizeof(block)) chunk = sizeof(block);
        memcpy(out + produced, block, chunk);
        produced += chunk;
        tls_hmac_sha256(secret, secret_len, a, sizeof(a), a);
    }
}

static uint8_t tls_aes_xtime(uint8_t value) {
    return (uint8_t)((value << 1) ^ (((value >> 7) & 1u) * 0x1Bu));
}

static uint8_t tls_aes_mul(uint8_t a, uint8_t b) {
    uint8_t result = 0;
    while (b) {
        if (b & 1u) result ^= a;
        a = tls_aes_xtime(a);
        b >>= 1;
    }
    return result;
}

static void tls_aes_key_expand(struct tls_aes_ctx *ctx, const uint8_t key[16]) {
    memcpy(ctx->round_keys, key, 16);
    int bytes = 16;
    int rcon = 1;
    uint8_t temp[4];

    while (bytes < 176) {
        memcpy(temp, ctx->round_keys + bytes - 4, 4);
        if ((bytes & 15) == 0) {
            uint8_t t = temp[0];
            temp[0] = g_tls_aes_sbox[temp[1]];
            temp[1] = g_tls_aes_sbox[temp[2]];
            temp[2] = g_tls_aes_sbox[temp[3]];
            temp[3] = g_tls_aes_sbox[t];
            temp[0] ^= g_tls_aes_rcon[rcon++];
        }
        for (int i = 0; i < 4; i++) {
            ctx->round_keys[bytes] = (uint8_t)(ctx->round_keys[bytes - 16] ^ temp[i]);
            bytes++;
        }
    }
}

static void tls_aes_add_round_key(uint8_t state[16], const uint8_t *round_key) {
    for (int i = 0; i < 16; i++) state[i] ^= round_key[i];
}

static void tls_aes_sub_bytes(uint8_t state[16]) {
    for (int i = 0; i < 16; i++) state[i] = g_tls_aes_sbox[state[i]];
}

static void tls_aes_inv_sub_bytes(uint8_t state[16]) {
    for (int i = 0; i < 16; i++) state[i] = g_tls_aes_inv_sbox[state[i]];
}

static void tls_aes_shift_rows(uint8_t state[16]) {
    uint8_t tmp[16];
    memcpy(tmp, state, 16);
    state[0]  = tmp[0];  state[4]  = tmp[4];  state[8]  = tmp[8];  state[12] = tmp[12];
    state[1]  = tmp[5];  state[5]  = tmp[9];  state[9]  = tmp[13]; state[13] = tmp[1];
    state[2]  = tmp[10]; state[6]  = tmp[14]; state[10] = tmp[2];  state[14] = tmp[6];
    state[3]  = tmp[15]; state[7]  = tmp[3];  state[11] = tmp[7];  state[15] = tmp[11];
}

static void tls_aes_inv_shift_rows(uint8_t state[16]) {
    uint8_t tmp[16];
    memcpy(tmp, state, 16);
    state[0]  = tmp[0];  state[4]  = tmp[4];  state[8]  = tmp[8];  state[12] = tmp[12];
    state[1]  = tmp[13]; state[5]  = tmp[1];  state[9]  = tmp[5];  state[13] = tmp[9];
    state[2]  = tmp[10]; state[6]  = tmp[14]; state[10] = tmp[2];  state[14] = tmp[6];
    state[3]  = tmp[7];  state[7]  = tmp[11]; state[11] = tmp[15]; state[15] = tmp[3];
}

static void tls_aes_mix_columns(uint8_t state[16]) {
    for (int c = 0; c < 4; c++) {
        uint8_t *col = state + c * 4;
        uint8_t a0 = col[0];
        uint8_t a1 = col[1];
        uint8_t a2 = col[2];
        uint8_t a3 = col[3];
        col[0] = (uint8_t)(tls_aes_mul(a0, 2) ^ tls_aes_mul(a1, 3) ^ a2 ^ a3);
        col[1] = (uint8_t)(a0 ^ tls_aes_mul(a1, 2) ^ tls_aes_mul(a2, 3) ^ a3);
        col[2] = (uint8_t)(a0 ^ a1 ^ tls_aes_mul(a2, 2) ^ tls_aes_mul(a3, 3));
        col[3] = (uint8_t)(tls_aes_mul(a0, 3) ^ a1 ^ a2 ^ tls_aes_mul(a3, 2));
    }
}

static void tls_aes_inv_mix_columns(uint8_t state[16]) {
    for (int c = 0; c < 4; c++) {
        uint8_t *col = state + c * 4;
        uint8_t a0 = col[0];
        uint8_t a1 = col[1];
        uint8_t a2 = col[2];
        uint8_t a3 = col[3];
        col[0] = (uint8_t)(tls_aes_mul(a0, 14) ^ tls_aes_mul(a1, 11) ^ tls_aes_mul(a2, 13) ^ tls_aes_mul(a3, 9));
        col[1] = (uint8_t)(tls_aes_mul(a0, 9) ^ tls_aes_mul(a1, 14) ^ tls_aes_mul(a2, 11) ^ tls_aes_mul(a3, 13));
        col[2] = (uint8_t)(tls_aes_mul(a0, 13) ^ tls_aes_mul(a1, 9) ^ tls_aes_mul(a2, 14) ^ tls_aes_mul(a3, 11));
        col[3] = (uint8_t)(tls_aes_mul(a0, 11) ^ tls_aes_mul(a1, 13) ^ tls_aes_mul(a2, 9) ^ tls_aes_mul(a3, 14));
    }
}

static void tls_aes_encrypt_block(const struct tls_aes_ctx *ctx, const uint8_t in[16], uint8_t out[16]) {
    uint8_t state[16];
    memcpy(state, in, 16);

    tls_aes_add_round_key(state, ctx->round_keys);
    for (int round = 1; round < 10; round++) {
        tls_aes_sub_bytes(state);
        tls_aes_shift_rows(state);
        tls_aes_mix_columns(state);
        tls_aes_add_round_key(state, ctx->round_keys + round * 16);
    }
    tls_aes_sub_bytes(state);
    tls_aes_shift_rows(state);
    tls_aes_add_round_key(state, ctx->round_keys + 160);
    memcpy(out, state, 16);
}

static void tls_aes_decrypt_block(const struct tls_aes_ctx *ctx, const uint8_t in[16], uint8_t out[16]) {
    uint8_t state[16];
    memcpy(state, in, 16);

    tls_aes_add_round_key(state, ctx->round_keys + 160);
    for (int round = 9; round > 0; round--) {
        tls_aes_inv_shift_rows(state);
        tls_aes_inv_sub_bytes(state);
        tls_aes_add_round_key(state, ctx->round_keys + round * 16);
        tls_aes_inv_mix_columns(state);
    }
    tls_aes_inv_shift_rows(state);
    tls_aes_inv_sub_bytes(state);
    tls_aes_add_round_key(state, ctx->round_keys);
    memcpy(out, state, 16);
}

static void tls_aes_cbc_encrypt(const struct tls_aes_ctx *ctx,
                                const uint8_t iv[16],
                                uint8_t *data,
                                size_t len) {
    uint8_t prev[16];
    memcpy(prev, iv, 16);
    for (size_t off = 0; off < len; off += 16) {
        for (int i = 0; i < 16; i++) data[off + (size_t)i] ^= prev[i];
        tls_aes_encrypt_block(ctx, data + off, data + off);
        memcpy(prev, data + off, 16);
    }
}

static void tls_aes_cbc_decrypt(const struct tls_aes_ctx *ctx,
                                const uint8_t iv[16],
                                uint8_t *data,
                                size_t len) {
    uint8_t prev[16];
    uint8_t cur[16];
    memcpy(prev, iv, 16);
    for (size_t off = 0; off < len; off += 16) {
        memcpy(cur, data + off, 16);
        tls_aes_decrypt_block(ctx, data + off, data + off);
        for (int i = 0; i < 16; i++) data[off + (size_t)i] ^= prev[i];
        memcpy(prev, cur, 16);
    }
}

static int tls_big_compare(const uint8_t *a, const uint8_t *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (a[i] == b[i]) continue;
        return (a[i] < b[i]) ? -1 : 1;
    }
    return 0;
}

static void tls_big_sub(uint8_t *a, const uint8_t *b, size_t len) {
    int borrow = 0;
    for (size_t i = len; i > 0; i--) {
        int value = (int)a[i - 1] - (int)b[i - 1] - borrow;
        if (value < 0) {
            value += 256;
            borrow = 1;
        } else {
            borrow = 0;
        }
        a[i - 1] = (uint8_t)value;
    }
}

static void tls_big_add(uint8_t *a, const uint8_t *b, size_t len) {
    int carry = 0;
    for (size_t i = len; i > 0; i--) {
        int value = (int)a[i - 1] + (int)b[i - 1] + carry;
        a[i - 1] = (uint8_t)(value & 0xFF);
        carry = value >> 8;
    }
}

static void tls_big_add_mod(uint8_t *acc, const uint8_t *value, const uint8_t *mod, size_t len) {
    tls_big_add(acc, value, len);
    while (tls_big_compare(acc, mod, len) >= 0) {
        tls_big_sub(acc, mod, len);
    }
}

static void tls_big_double_mod(uint8_t *value, const uint8_t *mod, size_t len) {
    int carry = 0;
    for (size_t i = len; i > 0; i--) {
        int v = ((int)value[i - 1] << 1) | carry;
        value[i - 1] = (uint8_t)(v & 0xFF);
        carry = (v >> 8) & 1;
    }
    while (carry || tls_big_compare(value, mod, len) >= 0) {
        tls_big_sub(value, mod, len);
        carry = 0;
    }
}

static void tls_big_mul_mod(const uint8_t *a, const uint8_t *b,
                            const uint8_t *mod, size_t len,
                            uint8_t *out) {
    uint8_t acc[256];
    uint8_t cur[256];

    if (len > sizeof(acc)) return;
    memset(acc, 0, sizeof(acc));
    memset(cur, 0, sizeof(cur));
    memcpy(cur + sizeof(cur) - len, a, len);

    for (size_t i = 0; i < len; i++) {
        uint8_t byte = b[len - 1 - i];
        for (int bit = 0; bit < 8; bit++) {
            if (byte & (1u << bit)) {
                tls_big_add_mod(acc + sizeof(acc) - len,
                                cur + sizeof(cur) - len,
                                mod,
                                len);
            }
            tls_big_double_mod(cur + sizeof(cur) - len, mod, len);
        }
    }

    memcpy(out, acc + sizeof(acc) - len, len);
}

static int tls_big_mod_exp(const uint8_t *base, size_t len,
                           uint32_t exponent,
                           const uint8_t *modulus,
                           uint8_t *out) {
    uint8_t result[256];
    uint8_t power[256];

    if (len > sizeof(result)) return 0;
    memset(result, 0, sizeof(result));
    memset(power, 0, sizeof(power));
    result[sizeof(result) - 1] = 1;
    memcpy(power + sizeof(power) - len, base, len);

    while (exponent) {
        if (exponent & 1u) {
            tls_big_mul_mod(result + sizeof(result) - len,
                            power + sizeof(power) - len,
                            modulus,
                            len,
                            result + sizeof(result) - len);
        }
        exponent >>= 1u;
        if (exponent) {
            tls_big_mul_mod(power + sizeof(power) - len,
                            power + sizeof(power) - len,
                            modulus,
                            len,
                            power + sizeof(power) - len);
        }
    }

    memcpy(out, result + sizeof(result) - len, len);
    return 1;
}

static int tls_asn1_read_len(const uint8_t *buf, size_t len, size_t *offset, size_t *out_len) {
    if (*offset >= len) return 0;
    uint8_t first = buf[(*offset)++];
    if ((first & 0x80u) == 0) {
        *out_len = first;
        return 1;
    }
    size_t bytes = first & 0x7Fu;
    size_t value = 0;
    if (bytes == 0 || bytes > sizeof(size_t) || *offset + bytes > len) return 0;
    while (bytes--) {
        value = (value << 8) | buf[(*offset)++];
    }
    *out_len = value;
    return 1;
}

static int tls_asn1_next(const uint8_t *buf, size_t len, size_t *offset,
                         uint8_t *tag, const uint8_t **value, size_t *value_len) {
    if (*offset >= len) return 0;
    *tag = buf[(*offset)++];
    if (!tls_asn1_read_len(buf, len, offset, value_len)) return 0;
    if (*offset + *value_len > len) return 0;
    *value = buf + *offset;
    *offset += *value_len;
    return 1;
}

static int tls_extract_rsa_pubkey(const uint8_t *cert, size_t cert_len,
                                  uint8_t *modulus, size_t *modulus_len,
                                  uint32_t *exponent) {
    static const uint8_t rsa_oid[] = {0x06,0x09,0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01};

    if (!cert || !modulus || !modulus_len || !exponent) return 0;
    for (size_t i = 0; i + sizeof(rsa_oid) < cert_len; i++) {
        if (memcmp(cert + i, rsa_oid, sizeof(rsa_oid)) != 0) continue;

        size_t scan = i + sizeof(rsa_oid);
        while (scan < cert_len && cert[scan] == 0x05) {
            uint8_t tag;
            const uint8_t *value;
            size_t value_len;
            size_t offset = scan;
            if (!tls_asn1_next(cert, cert_len, &offset, &tag, &value, &value_len)) break;
            scan = offset;
            if (tag != 0x05) break;
        }
        if (scan >= cert_len || cert[scan] != 0x03) continue;

        uint8_t tag;
        const uint8_t *bit_string;
        size_t bit_len;
        size_t offset = scan;
        if (!tls_asn1_next(cert, cert_len, &offset, &tag, &bit_string, &bit_len)) continue;
        if (tag != 0x03 || bit_len < 1 || bit_string[0] != 0x00) continue;

        const uint8_t *spki = bit_string + 1;
        size_t spki_len = bit_len - 1;
        size_t seq_off = 0;
        uint8_t seq_tag;
        const uint8_t *seq_val;
        size_t seq_len;
        if (!tls_asn1_next(spki, spki_len, &seq_off, &seq_tag, &seq_val, &seq_len)) continue;
        if (seq_tag != 0x30) continue;

        size_t int_off = 0;
        uint8_t tag1;
        const uint8_t *mod_val;
        size_t mod_len;
        if (!tls_asn1_next(seq_val, seq_len, &int_off, &tag1, &mod_val, &mod_len)) continue;
        if (tag1 != 0x02 || mod_len < TLS_RSA_ENC_MIN_LEN) continue;
        if (mod_val[0] == 0x00 && mod_len > 1) {
            mod_val++;
            mod_len--;
        }
        if (mod_len > *modulus_len) continue;

        uint8_t tag2;
        const uint8_t *exp_val;
        size_t exp_len;
        if (!tls_asn1_next(seq_val, seq_len, &int_off, &tag2, &exp_val, &exp_len)) continue;
        if (tag2 != 0x02 || exp_len == 0 || exp_len > 4) continue;

        uint32_t exp = 0;
        for (size_t j = 0; j < exp_len; j++) {
            exp = (exp << 8) | exp_val[j];
        }

        memcpy(modulus, mod_val, mod_len);
        *modulus_len = mod_len;
        *exponent = exp;
        return 1;
    }
    return 0;
}

static int tls_rsa_encrypt_premaster(const uint8_t *modulus, size_t modulus_len,
                                     uint32_t exponent,
                                     const uint8_t premaster[TLS_MASTER_SECRET_LEN],
                                     uint8_t *out) {
    uint8_t block[256];
    size_t pad_len;

    if (!modulus || !premaster || !out) return 0;
    if (modulus_len < TLS_MASTER_SECRET_LEN + 11 || modulus_len > sizeof(block)) return 0;

    memset(block, 0, sizeof(block));
    block[0] = 0x00;
    block[1] = 0x02;
    pad_len = modulus_len - TLS_MASTER_SECRET_LEN - 3;
    tls_random_bytes(block + 2, pad_len);
    for (size_t i = 0; i < pad_len; i++) {
        if (block[2 + i] == 0) block[2 + i] = (uint8_t)(i + 1);
    }
    block[2 + pad_len] = 0x00;
    memcpy(block + 3 + pad_len, premaster, TLS_MASTER_SECRET_LEN);
    return tls_big_mod_exp(block, modulus_len, exponent, modulus, out);
}

static int tls_tcp_recv_exact(int handle, uint8_t *buf, size_t len, uint32_t timeout_ms) {
    size_t got = 0;
    while (got < len) {
        ssize_t rc = net_tcp_recv(handle, buf + got, len - got, timeout_ms);
        if (rc <= 0) return 0;
        got += (size_t)rc;
    }
    return 1;
}

static int tls_send_plain_record(struct tls_session *session,
                                 uint8_t type,
                                 const void *data,
                                 size_t len,
                                 uint32_t timeout_ms) {
    uint8_t header[5];
    header[0] = type;
    tls_write_be16(header + 1, session->protocol_version);
    tls_write_be16(header + 3, (uint16_t)len);
    if (net_tcp_send(session->tcp_handle, header, sizeof(header), timeout_ms) != (ssize_t)sizeof(header)) {
        return 0;
    }
    if (len == 0) return 1;
    return net_tcp_send(session->tcp_handle, data, len, timeout_ms) == (ssize_t)len;
}

static int tls_send_encrypted_record(struct tls_session *session,
                                     uint8_t type,
                                     const uint8_t *plain,
                                     size_t plain_len,
                                     uint32_t timeout_ms) {
    uint8_t mac_input[13 + TLS_MAX_FRAGMENT_LEN];
    uint8_t iv[TLS_AES_BLOCK_LEN];
    uint8_t record[5 + TLS_AES_BLOCK_LEN + TLS_MAX_FRAGMENT_LEN + TLS_SHA256_LEN + TLS_AES_BLOCK_LEN];
    uint8_t mac[TLS_SHA256_LEN];
    size_t inner_len;
    size_t payload_len;
    size_t pad_len;

    if (plain_len > TLS_MAX_FRAGMENT_LEN) return 0;

    tls_write_be64(mac_input, session->write_seq);
    mac_input[8] = type;
    tls_write_be16(mac_input + 9, session->protocol_version);
    tls_write_be16(mac_input + 11, (uint16_t)plain_len);
    memcpy(mac_input + 13, plain, plain_len);
    tls_hmac_sha256(session->client_mac_key, TLS_MAC_KEY_LEN,
                    mac_input, 13 + plain_len, mac);

    tls_random_bytes(iv, sizeof(iv));
    memcpy(record + 5, iv, sizeof(iv));
    memcpy(record + 5 + sizeof(iv), plain, plain_len);
    memcpy(record + 5 + sizeof(iv) + plain_len, mac, sizeof(mac));
    inner_len = plain_len + sizeof(mac);
    pad_len = 15u - (inner_len % TLS_AES_BLOCK_LEN);
    for (size_t i = 0; i <= pad_len; i++) {
        record[5 + sizeof(iv) + inner_len + i] = (uint8_t)pad_len;
    }
    payload_len = sizeof(iv) + inner_len + pad_len + 1u;
    tls_aes_cbc_encrypt(&session->client_aes,
                        iv,
                        record + 5 + sizeof(iv),
                        payload_len - sizeof(iv));

    record[0] = type;
    tls_write_be16(record + 1, session->protocol_version);
    tls_write_be16(record + 3, (uint16_t)payload_len);
    session->write_seq++;

    return net_tcp_send(session->tcp_handle, record, 5 + payload_len, timeout_ms) == (ssize_t)(5 + payload_len);
}

static int tls_recv_record(struct tls_session *session,
                           uint8_t *type,
                           uint8_t *buf,
                           size_t *len,
                           size_t cap,
                           uint32_t timeout_ms,
                           int encrypted) {
    uint8_t header[5];

    if (!tls_tcp_recv_exact(session->tcp_handle, header, sizeof(header), timeout_ms)) return 0;
    *type = header[0];
    *len = tls_read_be16(header + 3);
    if (*len > cap) return 0;
    if (!tls_tcp_recv_exact(session->tcp_handle, buf, *len, timeout_ms)) return 0;

    if (!encrypted) return 1;
    if (*len < TLS_AES_BLOCK_LEN * 2u) return 0;

    uint8_t *iv = buf;
    uint8_t *cipher = buf + TLS_AES_BLOCK_LEN;
    size_t cipher_len = *len - TLS_AES_BLOCK_LEN;
    uint8_t *mac_input = (uint8_t *)kmalloc(13u + TLS_MAX_RECORD_LEN);
    uint8_t mac[TLS_SHA256_LEN];

    if (!mac_input) return 0;
    if ((cipher_len % TLS_AES_BLOCK_LEN) != 0) {
        kfree(mac_input);
        return 0;
    }
    tls_aes_cbc_decrypt(&session->server_aes, iv, cipher, cipher_len);

    uint8_t pad_byte = cipher[cipher_len - 1];
    size_t padding = (size_t)pad_byte + 1u;
    if (padding > cipher_len) {
        kfree(mac_input);
        return 0;
    }
    for (size_t i = 0; i < padding; i++) {
        if (cipher[cipher_len - 1u - i] != pad_byte) {
            kfree(mac_input);
            return 0;
        }
    }
    cipher_len -= padding;
    if (cipher_len < TLS_SHA256_LEN) {
        kfree(mac_input);
        return 0;
    }

    size_t plain_len = cipher_len - TLS_SHA256_LEN;
    tls_write_be64(mac_input, session->read_seq);
    mac_input[8] = *type;
    tls_write_be16(mac_input + 9, session->protocol_version);
    tls_write_be16(mac_input + 11, (uint16_t)plain_len);
    memcpy(mac_input + 13, cipher, plain_len);
    tls_hmac_sha256(session->server_mac_key, TLS_MAC_KEY_LEN,
                    mac_input, 13 + plain_len, mac);
    if (memcmp(mac, cipher + plain_len, TLS_SHA256_LEN) != 0) {
        kfree(mac_input);
        return 0;
    }

    session->read_seq++;
    memmove(buf, cipher, plain_len);
    *len = plain_len;
    kfree(mac_input);
    return 1;
}

static size_t tls_build_client_hello(struct tls_session *session, uint8_t *out, size_t cap) {
    uint8_t body[256];
    uint8_t ext[128];
    size_t body_len = 0;
    size_t ext_len = 0;
    size_t host_len = strlen(session->server_name);

    if (host_len > 0 && !tls_is_ipv4_text(session->server_name)) {
        uint8_t *sni = ext + ext_len;
        tls_write_be16(sni + 0, 0x0000);
        tls_write_be16(sni + 4, (uint16_t)(host_len + 3));
        tls_write_be16(sni + 6, (uint16_t)(host_len + 1));
        sni[8] = 0x00;
        tls_write_be16(sni + 9, (uint16_t)host_len);
        memcpy(sni + 11, session->server_name, host_len);
        tls_write_be16(sni + 2, (uint16_t)(host_len + 7));
        ext_len += 11 + host_len;
    }

    tls_write_be16(body + body_len, TLS_VERSION_1_2);
    body_len += 2;
    memcpy(body + body_len, session->client_random, TLS_RANDOM_LEN);
    body_len += TLS_RANDOM_LEN;
    body[body_len++] = 0;
    tls_write_be16(body + body_len, 2);
    body_len += 2;
    tls_write_be16(body + body_len, TLS_CIPHER_RSA_AES128_SHA256);
    body_len += 2;
    body[body_len++] = 1;
    body[body_len++] = 0;

    if (ext_len > 0) {
        tls_write_be16(body + body_len, (uint16_t)ext_len);
        body_len += 2;
        memcpy(body + body_len, ext, ext_len);
        body_len += ext_len;
    }

    if (cap < body_len + 4u) return 0;
    out[0] = TLS_HANDSHAKE_CLIENT_HELLO;
    tls_write_be24(out + 1, (uint32_t)body_len);
    memcpy(out + 4, body, body_len);
    return body_len + 4u;
}

static size_t tls_build_client_key_exchange(const uint8_t *encrypted,
                                            size_t encrypted_len,
                                            uint8_t *out,
                                            size_t cap) {
    size_t body_len = encrypted_len + 2u;
    if (cap < body_len + 4u || encrypted_len > 0xFFFFu) return 0;
    out[0] = TLS_HANDSHAKE_CLIENT_KEY_EXCH;
    tls_write_be24(out + 1, (uint32_t)body_len);
    tls_write_be16(out + 4, (uint16_t)encrypted_len);
    memcpy(out + 6, encrypted, encrypted_len);
    return body_len + 4u;
}

static size_t tls_build_finished(const uint8_t verify_data[TLS_VERIFY_DATA_LEN],
                                 uint8_t *out,
                                 size_t cap) {
    if (cap < TLS_VERIFY_DATA_LEN + 4u) return 0;
    out[0] = TLS_HANDSHAKE_FINISHED;
    tls_write_be24(out + 1, TLS_VERIFY_DATA_LEN);
    memcpy(out + 4, verify_data, TLS_VERIFY_DATA_LEN);
    return TLS_VERIFY_DATA_LEN + 4u;
}

static void tls_compute_finished(struct tls_session *session,
                                 const char *label,
                                 const uint8_t *transcript,
                                 size_t transcript_len,
                                 uint8_t out[TLS_VERIFY_DATA_LEN]) {
    uint8_t hash[TLS_SHA256_LEN];
    tls_sha256_digest(transcript, transcript_len, hash);
    tls_prf_sha256(session->master_secret, sizeof(session->master_secret),
                   label, hash, sizeof(hash), NULL, 0,
                   out, TLS_VERIFY_DATA_LEN);
}

static int tls_setup_keys(struct tls_session *session, const uint8_t premaster[TLS_MASTER_SECRET_LEN]) {
    uint8_t key_block[TLS_KEY_BLOCK_LEN];

    tls_prf_sha256(premaster, TLS_MASTER_SECRET_LEN, "master secret",
                   session->client_random, TLS_RANDOM_LEN,
                   session->server_random, TLS_RANDOM_LEN,
                   session->master_secret, TLS_MASTER_SECRET_LEN);

    tls_prf_sha256(session->master_secret, TLS_MASTER_SECRET_LEN, "key expansion",
                   session->server_random, TLS_RANDOM_LEN,
                   session->client_random, TLS_RANDOM_LEN,
                   key_block, sizeof(key_block));

    memcpy(session->client_mac_key, key_block, TLS_MAC_KEY_LEN);
    memcpy(session->server_mac_key, key_block + TLS_MAC_KEY_LEN, TLS_MAC_KEY_LEN);
    memcpy(session->client_key, key_block + (TLS_MAC_KEY_LEN * 2), TLS_AES_KEY_LEN);
    memcpy(session->server_key, key_block + (TLS_MAC_KEY_LEN * 2) + TLS_AES_KEY_LEN, TLS_AES_KEY_LEN);

    tls_aes_key_expand(&session->client_aes, session->client_key);
    tls_aes_key_expand(&session->server_aes, session->server_key);
    session->write_seq = 0;
    session->read_seq = 0;
    return 1;
}

static int tls_handshake(struct tls_session *session, uint32_t timeout_ms) {
    uint8_t *transcript = (uint8_t *)kmalloc(TLS_MAX_TRANSCRIPT_LEN);
    uint8_t *cert_copy = (uint8_t *)kmalloc(TLS_MAX_CERT_LEN);
    uint8_t *record_buf = (uint8_t *)kmalloc(TLS_MAX_RECORD_LEN);
    uint8_t client_hello[320];
    uint8_t premaster[TLS_MASTER_SECRET_LEN];
    uint8_t encrypted_premaster[256];
    uint8_t client_key_exchange[300];
    uint8_t finished[TLS_VERIFY_DATA_LEN];
    uint8_t finished_msg[32];
    size_t transcript_len = 0;
    size_t cert_len = 0;
    int done = 0;
    int saw_server_hello = 0;
    uint8_t modulus[256];
    size_t modulus_len = sizeof(modulus);
    uint32_t exponent = 0;

    if (!transcript || !cert_copy || !record_buf) {
        if (transcript) kfree(transcript);
        if (cert_copy) kfree(cert_copy);
        if (record_buf) kfree(record_buf);
        return NET_ERR_GENERIC;
    }

    tls_random_bytes(session->client_random, TLS_RANDOM_LEN);
    tls_write_be32(session->client_random, (uint32_t)timer_get_uptime_ms());

    size_t hello_len = tls_build_client_hello(session, client_hello, sizeof(client_hello));
    if (hello_len == 0 || !tls_send_plain_record(session, TLS_RECORD_HANDSHAKE, client_hello, hello_len, timeout_ms)) {
        kfree(transcript);
        kfree(cert_copy);
        kfree(record_buf);
        return NET_ERR_GENERIC;
    }
    memcpy(transcript, client_hello, hello_len);
    transcript_len = hello_len;

    while (!done) {
        uint8_t type = 0;
        size_t record_len = 0;
        if (!tls_recv_record(session, &type, record_buf, &record_len, TLS_MAX_RECORD_LEN, timeout_ms, 0) ||
            type != TLS_RECORD_HANDSHAKE) {
            kfree(transcript);
            kfree(cert_copy);
            kfree(record_buf);
            return NET_ERR_GENERIC;
        }

        size_t off = 0;
        while (off + 4 <= record_len) {
            uint8_t hs_type = record_buf[off];
            size_t hs_len = tls_read_be24(record_buf + off + 1);
            if (off + 4u + hs_len > record_len || transcript_len + 4u + hs_len > TLS_MAX_TRANSCRIPT_LEN) {
                kfree(transcript);
                kfree(cert_copy);
                kfree(record_buf);
                return NET_ERR_GENERIC;
            }

            memcpy(transcript + transcript_len, record_buf + off, 4u + hs_len);
            transcript_len += 4u + hs_len;

            if (hs_type == TLS_HANDSHAKE_SERVER_HELLO) {
                if (hs_len < 38u) {
                    kfree(transcript);
                    kfree(cert_copy);
                    kfree(record_buf);
                    return NET_ERR_GENERIC;
                }
                session->protocol_version = tls_read_be16(record_buf + off + 4);
                memcpy(session->server_random, record_buf + off + 6, TLS_RANDOM_LEN);
                size_t sid_len = record_buf[off + 38];
                if (39u + sid_len + 3u > hs_len) {
                    kfree(transcript);
                    kfree(cert_copy);
                    kfree(record_buf);
                    return NET_ERR_GENERIC;
                }
                session->cipher_suite = tls_read_be16(record_buf + off + 39u + sid_len);
                if (session->cipher_suite != TLS_CIPHER_RSA_AES128_SHA256) {
                    kfree(transcript);
                    kfree(cert_copy);
                    kfree(record_buf);
                    return NET_ERR_GENERIC;
                }
                saw_server_hello = 1;
            } else if (hs_type == TLS_HANDSHAKE_CERTIFICATE) {
                size_t chain_len = tls_read_be24(record_buf + off + 4);
                if (chain_len + 3u > hs_len || chain_len < 3u) {
                    kfree(transcript);
                    kfree(cert_copy);
                    kfree(record_buf);
                    return NET_ERR_GENERIC;
                }
                cert_len = tls_read_be24(record_buf + off + 7);
                if (cert_len == 0 || cert_len > TLS_MAX_CERT_LEN || cert_len + 6u > hs_len) {
                    kfree(transcript);
                    kfree(cert_copy);
                    kfree(record_buf);
                    return NET_ERR_GENERIC;
                }
                memcpy(cert_copy, record_buf + off + 10, cert_len);
            } else if (hs_type == TLS_HANDSHAKE_SERVER_HELLO_DONE) {
                done = 1;
            }

            off += 4u + hs_len;
        }
    }

    if (!saw_server_hello || cert_len == 0 ||
        !tls_extract_rsa_pubkey(cert_copy, cert_len, modulus, &modulus_len, &exponent)) {
        kfree(transcript);
        kfree(cert_copy);
        kfree(record_buf);
        return NET_ERR_GENERIC;
    }

    tls_random_bytes(premaster, sizeof(premaster));
    tls_write_be16(premaster, TLS_VERSION_1_2);
    if (!tls_setup_keys(session, premaster) ||
        !tls_rsa_encrypt_premaster(modulus, modulus_len, exponent, premaster, encrypted_premaster)) {
        kfree(transcript);
        kfree(cert_copy);
        kfree(record_buf);
        return NET_ERR_GENERIC;
    }

    size_t ckx_len = tls_build_client_key_exchange(encrypted_premaster, modulus_len,
                                                   client_key_exchange, sizeof(client_key_exchange));
    if (ckx_len == 0 || transcript_len + ckx_len > TLS_MAX_TRANSCRIPT_LEN) {
        kfree(transcript);
        kfree(cert_copy);
        kfree(record_buf);
        return NET_ERR_GENERIC;
    }
    memcpy(transcript + transcript_len, client_key_exchange, ckx_len);
    transcript_len += ckx_len;
    if (!tls_send_plain_record(session, TLS_RECORD_HANDSHAKE, client_key_exchange, ckx_len, timeout_ms)) {
        kfree(transcript);
        kfree(cert_copy);
        kfree(record_buf);
        return NET_ERR_GENERIC;
    }

    {
        uint8_t ccs = 1;
        if (!tls_send_plain_record(session, TLS_RECORD_CHANGE_CIPHER, &ccs, 1, timeout_ms)) {
            kfree(transcript);
            kfree(cert_copy);
            kfree(record_buf);
            return NET_ERR_GENERIC;
        }
    }

    tls_compute_finished(session, "client finished", transcript, transcript_len, finished);
    size_t finished_len = tls_build_finished(finished, finished_msg, sizeof(finished_msg));
    if (finished_len == 0 ||
        !tls_send_encrypted_record(session, TLS_RECORD_HANDSHAKE, finished_msg, finished_len, timeout_ms)) {
        kfree(transcript);
        kfree(cert_copy);
        kfree(record_buf);
        return NET_ERR_GENERIC;
    }
    if (transcript_len + finished_len > TLS_MAX_TRANSCRIPT_LEN) {
        kfree(transcript);
        kfree(cert_copy);
        kfree(record_buf);
        return NET_ERR_GENERIC;
    }
    memcpy(transcript + transcript_len, finished_msg, finished_len);
    transcript_len += finished_len;

    {
        uint8_t type = 0;
        size_t len = 0;
        if (!tls_recv_record(session, &type, record_buf, &len, TLS_MAX_RECORD_LEN, timeout_ms, 0) ||
            type != TLS_RECORD_CHANGE_CIPHER || len != 1 || record_buf[0] != 1) {
            kfree(transcript);
            kfree(cert_copy);
            kfree(record_buf);
            return NET_ERR_GENERIC;
        }
    }

    {
        uint8_t type = 0;
        size_t len = 0;
        uint8_t expected[TLS_VERIFY_DATA_LEN];
        if (!tls_recv_record(session, &type, record_buf, &len, TLS_MAX_RECORD_LEN, timeout_ms, 1) ||
            type != TLS_RECORD_HANDSHAKE || len < 4u + TLS_VERIFY_DATA_LEN ||
            record_buf[0] != TLS_HANDSHAKE_FINISHED ||
            tls_read_be24(record_buf + 1) != TLS_VERIFY_DATA_LEN) {
            kfree(transcript);
            kfree(cert_copy);
            kfree(record_buf);
            return NET_ERR_GENERIC;
        }
        tls_compute_finished(session, "server finished", transcript, transcript_len, expected);
        if (memcmp(expected, record_buf + 4, TLS_VERIFY_DATA_LEN) != 0) {
            kfree(transcript);
            kfree(cert_copy);
            kfree(record_buf);
            return NET_ERR_GENERIC;
        }
    }

    kfree(transcript);
    kfree(cert_copy);
    kfree(record_buf);
    return NET_OK;
}

static void tls_send_close_notify(struct tls_session *session, uint32_t timeout_ms) {
    uint8_t alert[2] = {1, TLS_ALERT_CLOSE_NOTIFY};
    (void)tls_send_encrypted_record(session, TLS_RECORD_ALERT, alert, sizeof(alert), timeout_ms);
}

int net_tls_probe_ipv4(const uint8_t addr[NET_IPV4_ADDR_LEN],
                       uint16_t port,
                       const char *server_name,
                       uint32_t flags,
                       uint32_t timeout_ms,
                       struct net_tls_result *out) {
    struct tls_session session;
    if (!addr || !out || port == 0) return NET_ERR_INVALID;
    if ((flags & NET_CLIENT_FLAG_INSECURE) == 0) return NET_ERR_INVALID;

    memset(&session, 0, sizeof(session));
    session.protocol_version = TLS_VERSION_1_2;
    tls_copy_string(session.server_name, server_name, sizeof(session.server_name));

    session.tcp_handle = net_tcp_connect_ipv4(addr, port, timeout_ms);
    if (session.tcp_handle < 0) return session.tcp_handle;

    int rc = tls_handshake(&session, timeout_ms);
    if (rc == NET_OK) {
        memset(out, 0, sizeof(*out));
        out->success = 1;
        out->secure = 1;
        out->protocol_version = session.protocol_version;
        out->cipher_suite = session.cipher_suite;
        out->remote_port = port;
        memcpy(out->remote_ip, addr, NET_IPV4_ADDR_LEN);
        tls_copy_string(out->server_name, session.server_name, sizeof(out->server_name));
        tls_send_close_notify(&session, timeout_ms);
    }

    (void)net_tcp_close(session.tcp_handle, timeout_ms);
    return rc;
}

static void tls_parse_http_headers(const uint8_t *data, size_t len, struct net_http_result *out) {
    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {
            out->body_offset = (uint32_t)(i + 4);
            break;
        }
    }
    if (out->body_offset == 0) return;

    char status_line[32];
    size_t i = 0;
    while (i < sizeof(status_line) - 1 && i < len && data[i] != '\r' && data[i] != '\n') {
        status_line[i] = (char)data[i];
        i++;
    }
    status_line[i] = '\0';
    const char *space = strstr(status_line, " ");
    if (space) out->status_code = (uint16_t)tls_parse_status_code(space + 1);
}

ssize_t net_http_get_ipv4(const struct net_http_request *request,
                          void *buf,
                          size_t len,
                          struct net_http_result *out) {
    struct tls_session session;
    uint8_t request_buf[512];
    uint8_t *record_buf = (uint8_t *)kmalloc(TLS_MAX_RECORD_LEN);
    size_t request_len = 0;
    size_t total = 0;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t timeout_ms;
    int secure;

    if (!request || !buf || !out || request->remote_port == 0) return NET_ERR_INVALID;
    if (!record_buf) return NET_ERR_GENERIC;
    secure = request->secure ? 1 : 0;
    timeout_ms = request->timeout_ms ? request->timeout_ms : 5000u;
    memset(&session, 0, sizeof(session));
    session.protocol_version = TLS_VERSION_1_2;
    tls_copy_string(session.server_name, request->host, sizeof(session.server_name));

    if (secure && (request->flags & NET_CLIENT_FLAG_INSECURE) == 0) return NET_ERR_INVALID;
    {
        static const char prefix1[] = "GET ";
        static const char prefix2[] = " HTTP/1.1\r\nHost: ";
        static const char suffix[] = "\r\nConnection: close\r\nUser-Agent: NumOS-Kernel\r\n\r\n";
        const char *path = request->path[0] ? request->path : "/";
        size_t need = strlen(prefix1) + strlen(path) + strlen(prefix2) + strlen(request->host) + strlen(suffix);
        if (strlen(request->host) == 0 || need > sizeof(request_buf)) return NET_ERR_INVALID;
        memcpy(request_buf + request_len, prefix1, strlen(prefix1));
        request_len += strlen(prefix1);
        memcpy(request_buf + request_len, path, strlen(path));
        request_len += strlen(path);
        memcpy(request_buf + request_len, prefix2, strlen(prefix2));
        request_len += strlen(prefix2);
        memcpy(request_buf + request_len, request->host, strlen(request->host));
        request_len += strlen(request->host);
        memcpy(request_buf + request_len, suffix, strlen(suffix));
        request_len += strlen(suffix);
    }

    session.tcp_handle = net_tcp_connect_ipv4(request->remote_ip, request->remote_port, timeout_ms);
    if (session.tcp_handle < 0) {
        kfree(record_buf);
        return session.tcp_handle;
    }

    if (secure) {
        int rc = tls_handshake(&session, timeout_ms);
        if (rc != NET_OK) {
            (void)net_tcp_close(session.tcp_handle, timeout_ms);
            kfree(record_buf);
            return rc;
        }
        if (!tls_send_encrypted_record(&session, TLS_RECORD_APPLICATION, request_buf, request_len, timeout_ms)) {
            tls_send_close_notify(&session, timeout_ms);
            (void)net_tcp_close(session.tcp_handle, timeout_ms);
            kfree(record_buf);
            return NET_ERR_GENERIC;
        }
    } else if (net_tcp_send(session.tcp_handle, request_buf, request_len, timeout_ms) != (ssize_t)request_len) {
        (void)net_tcp_close(session.tcp_handle, timeout_ms);
        kfree(record_buf);
        return NET_ERR_GENERIC;
    }

    memset(out, 0, sizeof(*out));
    out->secure = secure ? 1u : 0u;
    out->headers_included = (request->flags & NET_HTTP_FLAG_INCLUDE_HEADERS) ? 1u : 0u;
    out->protocol_version = secure ? session.protocol_version : 0;
    out->cipher_suite = secure ? session.cipher_suite : 0;
    out->remote_port = request->remote_port;
    memcpy(out->remote_ip, request->remote_ip, NET_IPV4_ADDR_LEN);

    if (secure) {
        for (;;) {
            uint8_t type = 0;
            size_t record_len = 0;
            if (!tls_recv_record(&session, &type, record_buf, &record_len, sizeof(record_buf), timeout_ms, 1) ||
                type == TLS_RECORD_ALERT) {
                break;
            }
            if (type != TLS_RECORD_APPLICATION) continue;
            size_t copy_len = record_len;
            if (copy_len > len - total) {
                copy_len = len - total;
                out->truncated = 1;
            }
            memcpy(dst + total, record_buf, copy_len);
            total += copy_len;
            if (total == len) break;
        }
        tls_send_close_notify(&session, timeout_ms);
    } else {
        for (;;) {
            ssize_t got = net_tcp_recv(session.tcp_handle, record_buf, sizeof(record_buf), timeout_ms);
            if (got <= 0) break;
            size_t copy_len = (size_t)got;
            if (copy_len > len - total) {
                copy_len = len - total;
                out->truncated = 1;
            }
            memcpy(dst + total, record_buf, copy_len);
            total += copy_len;
            if (total == len) break;
        }
    }

    (void)net_tcp_close(session.tcp_handle, timeout_ms);
    kfree(record_buf);
    out->bytes_received = (uint32_t)total;
    tls_parse_http_headers(dst, total, out);

    if ((request->flags & NET_HTTP_FLAG_INCLUDE_HEADERS) == 0 &&
        out->body_offset > 0 && out->body_offset < out->bytes_received) {
        size_t body_len = out->bytes_received - out->body_offset;
        memmove(dst, dst + out->body_offset, body_len);
        out->bytes_received = (uint32_t)body_len;
        out->body_offset = 0;
    }

    return (ssize_t)out->bytes_received;
}
