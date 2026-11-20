/* test_deflate_tune.cc - Test deflateTune() with small buffers */

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

#include "test_shared.h"

#include <gtest/gtest.h>

TEST(deflate, tune) {
    PREFIX3(stream) c_stream;
    uint8_t compr[128];
    z_size_t compr_len = sizeof(compr);
    int err;
    int good_length = 3;
    int max_lazy = 5;
    int nice_length = 18;
    int max_chain = 6;

    memset(&c_stream, 0, sizeof(c_stream));

    err = PREFIX(deflateInit)(&c_stream, Z_BEST_COMPRESSION);
    EXPECT_EQ(err, Z_OK);

    err = PREFIX(deflateTune)(&c_stream, good_length, max_lazy,nice_length, max_chain);
    EXPECT_EQ(err, Z_OK);

    c_stream.next_in = (z_const unsigned char *)hello;
    c_stream.next_out = compr;

    while (c_stream.total_in != hello_len && c_stream.total_out < compr_len) {
        c_stream.avail_in = c_stream.avail_out = 1; /* force small buffers */
        err = PREFIX(deflate)(&c_stream, Z_NO_FLUSH);
        EXPECT_EQ(err, Z_OK);
    }

    /* Finish the stream, still forcing small buffers: */
    for (;;) {
        c_stream.avail_out = 1;
        err = PREFIX(deflate)(&c_stream, Z_FINISH);
        if (err == Z_STREAM_END) break;
        EXPECT_EQ(err, Z_OK);
    }

    err = PREFIX(deflateEnd)(&c_stream);
    EXPECT_EQ(err, Z_OK);
}

TEST(deflate, tune_stepped_insert_count) {
    uint8_t input[4096], compressed[8192], output[sizeof(input)];
    memset(input, 'a', sizeof(input));

    const int levels[] = {2, 4, 6};
    const int32_t max_lazies[] = {INT32_MIN, 0, 1, 258, 100000};
    for (int level : levels) {
        for (int32_t max_lazy : max_lazies) {
            PREFIX3(stream) stream = {};
            ASSERT_EQ(PREFIX(deflateInit)(&stream, level), Z_OK);
            EXPECT_EQ(PREFIX(deflateTune)(&stream, 8, max_lazy, 128, 128), Z_OK);
            stream.next_in = input;
            stream.avail_in = sizeof(input);
            stream.next_out = compressed;
            stream.avail_out = sizeof(compressed);
            EXPECT_EQ(PREFIX(deflate)(&stream, Z_FINISH), Z_STREAM_END);
            z_size_t compressed_len = stream.total_out;
            EXPECT_EQ(PREFIX(deflateEnd)(&stream), Z_OK);

            z_uintmax_t output_len = sizeof(output);
            ASSERT_EQ(PREFIX(uncompress)(output, &output_len, compressed, (z_uintmax_t)compressed_len), Z_OK);
            EXPECT_EQ((size_t)output_len, sizeof(input));
            EXPECT_EQ(memcmp(input, output, sizeof(input)), 0);
        }
    }
}
