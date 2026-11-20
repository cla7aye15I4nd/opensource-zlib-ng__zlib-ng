/* test_deflate_params.cc - Test deflate() with dynamic change of compression level */

#include "zbuild.h"
#ifdef ZLIB_COMPAT
#  include "zlib.h"
#else
#  include "zlib-ng.h"
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "deflate.h"

#include <gtest/gtest.h>

#include "test_shared.h"

#define COMPR_BUFFER_SIZE (48 * 1024)
#define UNCOMPR_BUFFER_SIZE (64 * 1024)
#define UNCOMPR_RAND_SIZE (8 * 1024)

/* Fill a buffer with deterministic, effectively incompressible bytes. */
static void fill_random(uint8_t *buf, uint32_t len) {
    uint32_t r = 0xa5a5a5a5u;
    for (uint32_t i = 0; i < len; i++) {
        r = r * 1664525u + 1013904223u;
        buf[i] = (uint8_t)(r >> 24);
    }
}

TEST(deflate, params) {
    PREFIX3(stream) c_stream, d_stream;
    uint8_t *compr, *uncompr;
    uint32_t compr_len, uncompr_len;
    uint32_t diff;
    int err;
#ifndef ZLIB_COMPAT
    int level = -1;
    int strategy = -1;
    zng_deflate_param_value params[2];

    params[0].param = Z_DEFLATE_LEVEL;
    params[0].buf = &level;
    params[0].size = sizeof(level);

    params[1].param = Z_DEFLATE_STRATEGY;
    params[1].buf = &strategy;
    params[1].size = sizeof(strategy);
#endif

    memset(&c_stream, 0, sizeof(c_stream));
    memset(&d_stream, 0, sizeof(d_stream));

    compr = (uint8_t *)calloc(1, COMPR_BUFFER_SIZE);
    ASSERT_TRUE(compr != NULL);
    uncompr = (uint8_t *)calloc(1, UNCOMPR_BUFFER_SIZE);
    ASSERT_TRUE(uncompr != NULL);

    compr_len = COMPR_BUFFER_SIZE;
    uncompr_len = UNCOMPR_BUFFER_SIZE;

    fill_random(uncompr, UNCOMPR_RAND_SIZE);

    err = PREFIX(deflateInit)(&c_stream, Z_BEST_SPEED);
    EXPECT_EQ(err, Z_OK);

    c_stream.next_out = compr;
    c_stream.avail_out = compr_len;
    c_stream.next_in = uncompr;
    c_stream.avail_in = uncompr_len;

    err = PREFIX(deflate)(&c_stream, Z_NO_FLUSH);
    EXPECT_EQ(err, Z_OK);
    EXPECT_EQ(c_stream.avail_in, 0);

    /* Feed in already compressed data and switch to no compression: */
#ifndef ZLIB_COMPAT
    zng_deflateGetParams(&c_stream, params, sizeof(params) / sizeof(params[0]));
    EXPECT_EQ(level, Z_BEST_SPEED);
    EXPECT_EQ(strategy, Z_DEFAULT_STRATEGY);

    level = Z_NO_COMPRESSION;
    strategy = Z_DEFAULT_STRATEGY;
    zng_deflateSetParams(&c_stream, params, sizeof(params) / sizeof(params[0]));
#else
    PREFIX(deflateParams)(&c_stream, Z_NO_COMPRESSION, Z_DEFAULT_STRATEGY);
#endif

    c_stream.next_in = compr;
    diff = (unsigned int)(c_stream.next_out - compr);
    c_stream.avail_in = diff;
    err = PREFIX(deflate)(&c_stream, Z_NO_FLUSH);
    EXPECT_EQ(err, Z_OK);

    /* Switch back to compressing mode: */
#ifndef ZLIB_COMPAT
    level = -1;
    strategy = -1;
    zng_deflateGetParams(&c_stream, params, sizeof(params) / sizeof(params[0]));
    EXPECT_EQ(level, Z_NO_COMPRESSION);
    EXPECT_EQ(strategy, Z_DEFAULT_STRATEGY);

    level = Z_BEST_COMPRESSION;
    strategy = Z_FILTERED;
    zng_deflateSetParams(&c_stream, params, sizeof(params) / sizeof(params[0]));
#else
    PREFIX(deflateParams)(&c_stream, Z_BEST_COMPRESSION, Z_FILTERED);
#endif

    c_stream.next_in = uncompr;
    c_stream.avail_in = (unsigned int)uncompr_len;
    err = PREFIX(deflate)(&c_stream, Z_NO_FLUSH);
    EXPECT_EQ(err, Z_OK);

    err = PREFIX(deflate)(&c_stream, Z_FINISH);
    EXPECT_EQ(err, Z_STREAM_END);

    err = PREFIX(deflateEnd)(&c_stream);
    EXPECT_EQ(err, Z_OK);

    d_stream.next_in  = compr;
    d_stream.avail_in = (unsigned int)compr_len;

    err = PREFIX(inflateInit)(&d_stream);
    EXPECT_EQ(err, Z_OK);

    do {
        d_stream.next_out = uncompr;            /* discard the output */
        d_stream.avail_out = uncompr_len;
        err = PREFIX(inflate)(&d_stream, Z_NO_FLUSH);
        if (err == Z_STREAM_END)
            break;
        EXPECT_EQ(err, Z_OK);
    } while (err == Z_OK);

    err = PREFIX(inflateEnd)(&d_stream);
    EXPECT_EQ(err, Z_OK);

    EXPECT_EQ(d_stream.total_out, (2 * uncompr_len) + diff);

    free(compr);
    free(uncompr);
}

/* deflateParams must preserve match history when the params change mid-stream.
 * The second half of the input is an exact copy of the first and sits within
 * the 32K window, so preserved history turns it into cross-boundary matches.
 * If the chains were dropped at the switch the copy is re-emitted and the
 * stream nearly doubles. */
static void deflate_params_history(int from_level, int from_strategy, int to_level, int to_strategy) {
    const uint32_t half = 16 * 1024;
    const uint32_t len = 2 * half;

    uint8_t *uncompr = (uint8_t *)calloc(1, len);
    uint8_t *compr = (uint8_t *)calloc(1, 2 * len);
    uint8_t *check = (uint8_t *)calloc(1, len);
    ASSERT_TRUE(uncompr != NULL && compr != NULL && check != NULL);

    fill_random(uncompr, half);
    memcpy(uncompr + half, uncompr, half);

    PREFIX3(stream) c_stream;
    memset(&c_stream, 0, sizeof(c_stream));
    EXPECT_EQ(PREFIX(deflateInit2)(&c_stream, from_level, Z_DEFLATED, 15, 8, from_strategy), Z_OK);

    c_stream.next_out = compr;
    c_stream.avail_out = 2 * len;
    c_stream.next_in = uncompr;
    c_stream.avail_in = half;
    EXPECT_EQ(PREFIX(deflate)(&c_stream, Z_NO_FLUSH), Z_OK);
    EXPECT_EQ(c_stream.avail_in, 0u);

    EXPECT_EQ(PREFIX(deflateParams)(&c_stream, to_level, to_strategy), Z_OK);

    c_stream.next_in = uncompr + half;
    c_stream.avail_in = half;
    EXPECT_EQ(PREFIX(deflate)(&c_stream, Z_FINISH), Z_STREAM_END);

    uint32_t compr_len = (uint32_t)(c_stream.next_out - compr);
    EXPECT_EQ(PREFIX(deflateEnd)(&c_stream), Z_OK);

    PREFIX3(stream) d_stream;
    memset(&d_stream, 0, sizeof(d_stream));
    EXPECT_EQ(PREFIX(inflateInit)(&d_stream), Z_OK);
    d_stream.next_in = compr;
    d_stream.avail_in = compr_len;
    d_stream.next_out = check;
    d_stream.avail_out = len;
    EXPECT_EQ(PREFIX(inflate)(&d_stream, Z_FINISH), Z_STREAM_END);
    EXPECT_EQ(d_stream.total_out, len);
    EXPECT_EQ(memcmp(uncompr, check, len), 0);
    EXPECT_EQ(PREFIX(inflateEnd)(&d_stream), Z_OK);

    EXPECT_LT(compr_len, half + half / 2);

    free(uncompr);
    free(compr);
    free(check);
}

TEST(deflate, params_history_level9_down) {
    deflate_params_history(9, Z_DEFAULT_STRATEGY, 6, Z_DEFAULT_STRATEGY);
}

TEST(deflate, params_history_level9_up) {
    deflate_params_history(6, Z_DEFAULT_STRATEGY, 9, Z_DEFAULT_STRATEGY);
}

/* Switching from a hashless strategy (Z_RLE/Z_HUFFMAN_ONLY) back to a hashing one
 * at the same level must also rebuild, since those strategies never insert. */
TEST(deflate, params_history_rle) {
    deflate_params_history(6, Z_RLE, 6, Z_DEFAULT_STRATEGY);
}

TEST(deflate, params_history_huffman) {
    deflate_params_history(6, Z_HUFFMAN_ONLY, 6, Z_DEFAULT_STRATEGY);
}

/* Level 0 fills the window buffer, which is larger than 2 * w_size for small windows. The next
 * matching level must refill from that position without reading past the buffer. */
static void deflate_params_from_stored(int window_bits, int to_level) {
    const uint32_t len = 128 * 1024;
    const uint32_t compr_size = 2 * len;

    uint8_t *uncompr = (uint8_t *)calloc(1, len);
    uint8_t *compr = (uint8_t *)calloc(1, compr_size);
    uint8_t *check = (uint8_t *)calloc(1, len);
    ASSERT_TRUE(uncompr != NULL && compr != NULL && check != NULL);

    fill_random(uncompr, len);

    PREFIX3(stream) c_stream;
    memset(&c_stream, 0, sizeof(c_stream));
    EXPECT_EQ(PREFIX(deflateInit2)(&c_stream, 0, Z_DEFLATED, window_bits, 8, Z_DEFAULT_STRATEGY), Z_OK);

    /* A small output buffer keeps level 0 from copying straight through, so input fills the
     * window */
    c_stream.next_out = compr;
    c_stream.avail_out = 16;
    c_stream.next_in = uncompr;
    c_stream.avail_in = len;
    EXPECT_EQ(PREFIX(deflate)(&c_stream, Z_NO_FLUSH), Z_OK);

    /* Hold back the rest so the switch leaves level 0's data in the window */
    uint32_t remaining = c_stream.avail_in;
    c_stream.avail_in = 0;
    c_stream.avail_out = compr_size - (uint32_t)(c_stream.next_out - compr);
    EXPECT_EQ(PREFIX(deflateParams)(&c_stream, to_level, Z_DEFAULT_STRATEGY), Z_OK);

    c_stream.avail_in = remaining;
    EXPECT_EQ(PREFIX(deflate)(&c_stream, Z_FINISH), Z_STREAM_END);

    uint32_t compr_len = (uint32_t)(c_stream.next_out - compr);
    EXPECT_EQ(PREFIX(deflateEnd)(&c_stream), Z_OK);

    PREFIX3(stream) d_stream;
    memset(&d_stream, 0, sizeof(d_stream));
    EXPECT_EQ(PREFIX(inflateInit2)(&d_stream, window_bits), Z_OK);
    d_stream.next_in = compr;
    d_stream.avail_in = compr_len;
    d_stream.next_out = check;
    d_stream.avail_out = len;
    EXPECT_EQ(PREFIX(inflate)(&d_stream, Z_FINISH), Z_STREAM_END);
    EXPECT_EQ(d_stream.total_out, len);
    EXPECT_EQ(memcmp(uncompr, check, len), 0);
    EXPECT_EQ(PREFIX(inflateEnd)(&d_stream), Z_OK);

    free(uncompr);
    free(compr);
    free(check);
}

TEST(deflate, params_from_stored) {
    for (int window_bits = 9; window_bits <= 15; window_bits++) {
        for (int level : {1, 2, 6, 9}) {
            SCOPED_TRACE(testing::Message() << "window bits " << window_bits << ", level " << level);
            deflate_params_from_stored(window_bits, level);
        }
    }
}

static void verify_stale_prev_after_reset(uint32_t seed, bool use_reset) {
    uint64_t state = (uint64_t)seed * 7919ULL + 3ULL;
    auto rnd = [&state]() -> unsigned {
        state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        return (unsigned)(state >> 33);
    };
    static uint8_t a[70000], b[70000], decoded[140000], compressed[1 << 18];
    int la = 2 + rnd() % 5, lb = 7 + rnd() % 2, alpha1 = 2 + rnd() % 6, alpha2 = 2 + rnd() % 4;
    (void)rnd();
    size_t na = 33000 + rnd() % 31000, nb = 34000 + rnd() % 30000, sa = 2000 + rnd() % 20000;
    for (size_t k = 0; k < na; k++)
        a[k] = 'a' + rnd() % alpha1;
    size_t k = 0;
    while (k < sa) {
        size_t run = 300 + rnd() % 3000, txt = 10 + rnd() % 300;
        int per = 1 + rnd() % 12;
        unsigned char pat[16];
        for (int j = 0; j < per; j++)
            pat[j] = 'a' + rnd() % alpha2;
        for (size_t j = 0; j < run && k < sa; j++)
            b[k++] = pat[j % per];
        for (size_t j = 0; j < txt && k < sa; j++)
            b[k++] = 'a' + rnd() % alpha2;
    }
    while (k < nb) {
        int kind = rnd() % 3;
        if (kind == 0) {
            size_t off = rnd() % sa, len = 4 + rnd() % 8;
            for (size_t j = 0; j < len && k < nb; j++)
                b[k++] = b[off + j];
            if (k < nb)
                b[k++] = 'a' + rnd() % (alpha2 + 1);
        } else if (kind == 1) {
            size_t len = 1 + rnd() % 40;
            for (size_t j = 0; j < len && k < nb; j++)
                b[k++] = 'a' + rnd() % alpha2;
        } else {
            size_t off = rnd() % k, len = 3 + rnd() % 30;
            for (size_t j = 0; j < len && k < nb; j++, k++)
                b[k] = b[off + j];
        }
    }
    PREFIX3(stream) stream = {};
    ASSERT_EQ(PREFIX(deflateInit2)(&stream, la, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY), Z_OK);
    stream.next_out = compressed;
    stream.avail_out = sizeof(compressed);
    stream.next_in = a;
    stream.avail_in = (uint32_t)na;
    if (use_reset) {
        EXPECT_EQ(PREFIX(deflate)(&stream, Z_NO_FLUSH), Z_OK);
        EXPECT_EQ(PREFIX(deflateReset)(&stream), Z_OK);
        stream.next_out = compressed;
        stream.avail_out = sizeof(compressed);
    } else {
        EXPECT_EQ(PREFIX(deflate)(&stream, Z_FULL_FLUSH), Z_OK);
    }
    stream.next_in = b;
    stream.avail_in = (uint32_t)sa;
    EXPECT_EQ(PREFIX(deflate)(&stream, Z_NO_FLUSH), Z_OK);
    EXPECT_EQ(PREFIX(deflateParams)(&stream, lb, Z_DEFAULT_STRATEGY), Z_OK);
    stream.next_in = b + sa;
    stream.avail_in = (uint32_t)(nb - sa);
    EXPECT_EQ(PREFIX(deflate)(&stream, Z_FINISH), Z_STREAM_END);
    uint32_t compressed_len = (uint32_t)stream.total_out;
    EXPECT_EQ(PREFIX(deflateEnd)(&stream), Z_OK);

    PREFIX3(stream) decoder = {};
    ASSERT_EQ(PREFIX(inflateInit2)(&decoder, -MAX_WBITS), Z_OK);
    decoder.next_in = compressed;
    decoder.avail_in = compressed_len;
    decoder.next_out = decoded;
    decoder.avail_out = sizeof(decoded);
    EXPECT_EQ(PREFIX(inflate)(&decoder, Z_FINISH), Z_STREAM_END);
    if (use_reset) {
        ASSERT_EQ(decoder.total_out, nb);
        EXPECT_EQ(memcmp(decoded, b, nb), 0);
    } else {
        ASSERT_EQ(decoder.total_out, na + nb);
        EXPECT_EQ(memcmp(decoded, a, na), 0);
        EXPECT_EQ(memcmp(decoded + na, b, nb), 0);
    }
    EXPECT_EQ(PREFIX(inflateEnd)(&decoder), Z_OK);
}

TEST(deflate, changes_level_after_full_flush) {
    verify_stale_prev_after_reset(362, false);
}

TEST(deflate, changes_level_after_reset) {
    verify_stale_prev_after_reset(303, true);
}
