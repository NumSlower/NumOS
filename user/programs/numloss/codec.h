#ifndef NUMLOSS_CODEC_H
#define NUMLOSS_CODEC_H

#include "libc.h"

#define NUMLOSS_HEADER_SIZE 16u
#define NUMLOSS_VERSION_V1 1u
#define NUMLOSS_VERSION_V2 2u
#define NUMLOSS_VERSION_V3 3u
#define NUMLOSS_VERSION_V4 4u
#define NUMLOSS_MAX_INPUT_BYTES (256u * 1024u)
#define NUMLOSS_MAX_ARCHIVE_BYTES \
    (NUMLOSS_HEADER_SIZE + NUMLOSS_MAX_INPUT_BYTES + (NUMLOSS_MAX_INPUT_BYTES / 64u) + 64u)

/* ------------------------------------------------------------------
 * Pre-compression transforms stored in the archive header (byte 5).
 *
 * GROUP<N> separates the byte streams of N-byte words before
 * compression (see Matt et al., "Lossless Compression of Time Series
 * Data", arXiv:2510.07015, and Hershcovitch et al., "ZipNN",
 * arXiv:2411.05239).  Delta and XOR decorrelation then operate within
 * each separated lane.
 *
 * DELTA8_DELTA8 applies delta coding twice (second-order / delta-of-
 * delta), which is effective when the first-order delta is itself a
 * slowly varying signal — a pattern common in smoothly accelerating
 * sensor streams (ibid., §IV-B).
 * ------------------------------------------------------------------ */
#define NUMLOSS_TRANSFORM_RAW             0u
#define NUMLOSS_TRANSFORM_DELTA8          1u
#define NUMLOSS_TRANSFORM_XOR8            2u
#define NUMLOSS_TRANSFORM_GROUP4          3u
#define NUMLOSS_TRANSFORM_GROUP4_DELTA8   4u
#define NUMLOSS_TRANSFORM_GROUP4_XOR8     5u
#define NUMLOSS_TRANSFORM_GROUP8          6u
#define NUMLOSS_TRANSFORM_GROUP8_DELTA8   7u
#define NUMLOSS_TRANSFORM_GROUP8_XOR8     8u

/* 2-byte / 16-bit word grouping: splits each pair of bytes into
 * separate lanes before entropy coding.  Particularly useful for
 * 16-bit sensor ADC readings where the high byte is nearly constant
 * and the low byte changes rapidly. */
#define NUMLOSS_TRANSFORM_GROUP2          9u
#define NUMLOSS_TRANSFORM_GROUP2_DELTA8   10u
#define NUMLOSS_TRANSFORM_GROUP2_XOR8     11u

/* Second-order delta (delta of the first-order delta residuals).
 * Reduces entropy further when acceleration — not just velocity — is
 * nearly constant, as shown in the time series benchmark
 * (arXiv:2510.07015, Table I, "Sine" signal column). */
#define NUMLOSS_TRANSFORM_DELTA8_DELTA8   12u
#define NUMLOSS_TRANSFORM_TEXT_PROSE      13u
#define NUMLOSS_TRANSFORM_TEXT_CODE       14u

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
