#ifndef NUMLOSS_CODEC_H
#define NUMLOSS_CODEC_H

#include "libc.h"

#define NUMLOSS_HEADER_SIZE 16u
#define NUMLOSS_VERSION_V1 1u
#define NUMLOSS_VERSION_V2 2u
#define NUMLOSS_VERSION_V3 3u
#define NUMLOSS_MAX_INPUT_BYTES (256u * 1024u)
#define NUMLOSS_MAX_ARCHIVE_BYTES \
    (NUMLOSS_HEADER_SIZE + NUMLOSS_MAX_INPUT_BYTES + (NUMLOSS_MAX_INPUT_BYTES / 64u) + 64u)

#define NUMLOSS_TRANSFORM_RAW 0u
#define NUMLOSS_TRANSFORM_DELTA8 1u
#define NUMLOSS_TRANSFORM_XOR8 2u
#define NUMLOSS_TRANSFORM_GROUP4 3u
#define NUMLOSS_TRANSFORM_GROUP4_DELTA8 4u
#define NUMLOSS_TRANSFORM_GROUP4_XOR8 5u
#define NUMLOSS_TRANSFORM_GROUP8 6u
#define NUMLOSS_TRANSFORM_GROUP8_DELTA8 7u
#define NUMLOSS_TRANSFORM_GROUP8_XOR8 8u

enum numloss_status {
    NUMLOSS_OK = 0,
    NUMLOSS_ERR_ARGS = -1,
    NUMLOSS_ERR_INPUT = -2,
    NUMLOSS_ERR_OUTPUT = -3,
    NUMLOSS_ERR_FORMAT = -4
};

int numloss_read_header(const uint8_t *input, uint32_t input_size,
                        uint32_t *original_size, uint32_t *payload_size);

int numloss_encode(const uint8_t *input, uint32_t input_size,
                   uint8_t *output, uint32_t output_cap,
                   uint32_t *output_size);

int numloss_decode(const uint8_t *input, uint32_t input_size,
                   uint8_t *output, uint32_t output_cap,
                   uint32_t *output_size);

#endif
