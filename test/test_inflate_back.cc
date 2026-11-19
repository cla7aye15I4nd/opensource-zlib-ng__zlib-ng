/* test_inflate_back.cc - Test inflateBack() with safe mode match bailout
 * Verifies that inflateBack() correctly handles the MATCH state when
 * inflate_fast() bails out due to insufficient output space.
 */

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
#include <vector>

#include <gtest/gtest.h>

/* Callback to provide all input at once */
static z_uint32_t pull_all(void *desc, z_const unsigned char **buf) {
    PREFIX3(stream) *strm = (PREFIX3(stream) *)desc;
    if (strm->avail_in == 0)
        return 0;
    *buf = strm->next_in;
    unsigned avail = strm->avail_in;
    strm->avail_in = 0;
    return avail;
}

struct output_state {
    uint8_t *buf;
    size_t size;
    size_t pos;
};

/* Callback to collect all output */
static z_int32_t push_all(void *desc, unsigned char *buf, z_uint32_t len) {
    struct output_state *out = (struct output_state *)desc;
    if (out->pos + len > out->size)
        return 1;  /* overflow */
    memcpy(out->buf + out->pos, buf, len);
    out->pos += len;
    return 0;
}

class inflate_back : public ::testing::Test {
protected:
    static const int wbits = 9;
    static const uint32_t wsize = 1U << wbits;
    static const uint32_t data_size = wsize * 64;

    uint8_t *window = nullptr;
    uint8_t *decompressed = nullptr;
    uint8_t *original = nullptr;
    uint8_t *compressed = nullptr;

    void SetUp() override {
        window = (uint8_t *)malloc(wsize);
        decompressed = (uint8_t *)malloc(data_size);
        original = (uint8_t *)malloc(data_size);
        ASSERT_NE(window, nullptr);
        ASSERT_NE(decompressed, nullptr);
        ASSERT_NE(original, nullptr);

        /* Generate data that triggers MATCH bailout */
        /* 5-byte repeating pattern -> len=258 dist=5 tokens */
        for (uint32_t i = 0; i < data_size; i++)
            original[i] = (uint8_t)(i % 5);
    }

    void TearDown() override {
        free(compressed);
        free(original);
        free(decompressed);
        free(window);
    }
};

/* Test that inflateBack() handles the MATCH state when inflate_fast() bails
 * out due to insufficient output space. */
TEST_F(inflate_back, match_state) {
    int err;

    /* Compress with raw deflate (no zlib/gzip header) */
    PREFIX3(stream) c_strm;
    memset(&c_strm, 0, sizeof(c_strm));
    err = PREFIX(deflateInit2)(&c_strm, Z_BEST_COMPRESSION, Z_DEFLATED,
                               -wbits, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
    ASSERT_EQ(err, Z_OK);

    z_uintmax_t comp_bound = PREFIX(deflateBound)(&c_strm, data_size);
    compressed = (uint8_t *)malloc(comp_bound);
    ASSERT_NE(compressed, nullptr);

    c_strm.next_in = original;
    c_strm.avail_in = data_size;
    c_strm.next_out = compressed;
    c_strm.avail_out = (uint32_t)comp_bound;

    err = PREFIX(deflate)(&c_strm, Z_FINISH);
    EXPECT_EQ(err, Z_STREAM_END);
    uint32_t compressed_size = (uint32_t)c_strm.total_out;
    PREFIX(deflateEnd)(&c_strm);

    /* Decompress with inflateBack() */
    PREFIX3(stream) d_strm;
    memset(&d_strm, 0, sizeof(d_strm));

    err = PREFIX(inflateBackInit)(&d_strm, wbits, window);
    ASSERT_EQ(err, Z_OK);

    d_strm.next_in = compressed;
    d_strm.avail_in = compressed_size;

    struct output_state out_state;
    out_state.buf = decompressed;
    out_state.size = data_size;
    out_state.pos = 0;

    err = PREFIX(inflateBack)(&d_strm, pull_all, &d_strm, push_all, &out_state);
    EXPECT_EQ(err, Z_STREAM_END);

    PREFIX(inflateBackEnd)(&d_strm);

    /* Verify decompressed output matches original */
    EXPECT_EQ(out_state.pos, (size_t)data_size);
    EXPECT_EQ(memcmp(decompressed, original, data_size), 0);
}

/* Test that inflateBack() rejects a distance that reaches past its window when
 * inflate_fast() bails out to the MATCH state, instead of copying from before
 * the caller-supplied window. */
TEST_F(inflate_back, match_state_invalid_distance) {
    int err;
    /* Prefix length is chosen so the repeat below starts late in a window pass,
     * leaving fewer than 258 bytes of window space and forcing inflate_fast()
     * into its MATCH bailout. */
    const uint32_t small_wbits = 8;
    const uint32_t small_wsize = 1U << small_wbits;
    const uint32_t prefix = small_wsize + 144;
    const uint32_t src_size = prefix + 258 + 42;

    /* Unpredictable prefix so the first long-distance match is the one below,
     * then a 258-byte repeat of the start, giving dist > wsize. */
    uint32_t seed = 0x12345678;
    for (uint32_t i = 0; i < src_size; i++) {
        seed = seed * 1103515245 + 12345;
        original[i] = (uint8_t)(seed >> 16);
    }
    memcpy(original + prefix, original, 258);

    /* Compress with a full-size window so the encoder may use the long distance */
    PREFIX3(stream) c_strm;
    memset(&c_strm, 0, sizeof(c_strm));
    err = PREFIX(deflateInit2)(&c_strm, Z_BEST_COMPRESSION, Z_DEFLATED,
                               -MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
    ASSERT_EQ(err, Z_OK);

    z_uintmax_t comp_bound = PREFIX(deflateBound)(&c_strm, src_size);
    compressed = (uint8_t *)malloc(comp_bound);
    ASSERT_NE(compressed, nullptr);

    c_strm.next_in = original;
    c_strm.avail_in = src_size;
    c_strm.next_out = compressed;
    c_strm.avail_out = (uint32_t)comp_bound;

    err = PREFIX(deflate)(&c_strm, Z_FINISH);
    EXPECT_EQ(err, Z_STREAM_END);
    uint32_t compressed_size = (uint32_t)c_strm.total_out;
    PREFIX(deflateEnd)(&c_strm);

    /* Decompress with a window too small for that distance */
    PREFIX3(stream) d_strm;
    memset(&d_strm, 0, sizeof(d_strm));

    uint8_t *small_window = (uint8_t *)malloc(small_wsize);
    ASSERT_NE(small_window, nullptr);

    err = PREFIX(inflateBackInit)(&d_strm, small_wbits, small_window);
    ASSERT_EQ(err, Z_OK);

    d_strm.next_in = compressed;
    d_strm.avail_in = compressed_size;

    struct output_state out_state;
    out_state.buf = decompressed;
    out_state.size = data_size;
    out_state.pos = 0;

    err = PREFIX(inflateBack)(&d_strm, pull_all, &d_strm, push_all, &out_state);
    EXPECT_EQ(err, Z_DATA_ERROR);

    PREFIX(inflateBackEnd)(&d_strm);
    free(small_window);
}

namespace {

/* Raw deflate: a stored 0xA5, then a final fixed block that copies 3 bytes. */
class raw_bits {
public:
    void put(uint32_t val, unsigned n) {
        for (unsigned i = 0; i < n; i++) {
            if (val & 1)
                data[nbit >> 3] |= (uint8_t)(1u << (nbit & 7));
            val >>= 1;
            nbit++;
        }
    }

    /* Huffman codes are packed most-significant bit first. */
    void huff(uint32_t code, unsigned n) {
        for (int i = (int)n - 1; i >= 0; i--)
            put((code >> i) & 1, 1);
    }

    unsigned size() const {
        return (nbit + 7) >> 3;
    }

    uint8_t data[32] = {};
    unsigned nbit = 0;
};

static unsigned craft_stored_then_match(uint8_t *out, unsigned dist) {
    raw_bits b;

    b.put(0, 1);             /* BFINAL */
    b.put(0, 2);             /* stored */
    while (b.nbit & 7)
        b.put(0, 1);
    b.put(1, 16);            /* LEN */
    b.put(0xffffu ^ 1u, 16); /* NLEN */
    b.put(0xa5, 8);

    b.put(1, 1);             /* final */
    b.put(1, 2);             /* fixed */
    b.huff(1, 7);            /* length symbol 257: 3 bytes, code 0000001 */
    b.huff(dist - 1, 5);     /* distance symbol, no extra bits for 1 or 2 */
    b.huff(0, 7);            /* end of block */

    unsigned n = b.size();
    memcpy(out, b.data, n);
    return n;
}

static int inflate_back_raw(const uint8_t *raw, unsigned n, unsigned pad, uint8_t *decoded, unsigned *produced) {
    const int wbits = 15;
    uint8_t *window = (uint8_t *)malloc(1u << wbits);
    uint8_t input[96];
    PREFIX3(stream) strm;
    struct output_state out;
    int err;

    if (window == NULL)
        return Z_MEM_ERROR;
    memset(window, 0x5a, 1u << wbits);
    memcpy(input, raw, n);
    if (pad)
        memset(input + n, 0, pad);

    memset(&strm, 0, sizeof(strm));
    err = PREFIX(inflateBackInit)(&strm, wbits, window);
    if (err != Z_OK) {
        free(window);
        return err;
    }
    strm.next_in = input;
    strm.avail_in = n + pad;
    out.buf = decoded;
    out.size = 64;
    out.pos = 0;

    err = PREFIX(inflateBack)(&strm, pull_all, &strm, push_all, &out);
    PREFIX(inflateBackEnd)(&strm);
    free(window);
    *produced = (unsigned)out.pos;
    return err;
}

}  // namespace

/* One byte exists. A distance of 2 starts before that byte. Enough trailing
 * input is left that inflateBack() takes inflate_fast(), which used to treat
 * the byte already written in this window as history before the window. */
TEST(inflate_back_distance, too_far_before_fast_path) {
    uint8_t raw[32];
    uint8_t decoded[64];
    unsigned n = craft_stored_then_match(raw, 2);
    unsigned produced = 0;

    EXPECT_EQ(inflate_back_raw(raw, n, 32, decoded, &produced), Z_DATA_ERROR);
}

/* Same stream with too little input for inflate_fast(). 2.3.x only checked
 * this when INFLATE_STRICT was enabled; develop already checks it. */
TEST(inflate_back_distance, too_far_on_slow_path) {
    uint8_t raw[32];
    uint8_t decoded[64];
    unsigned n = craft_stored_then_match(raw, 2);
    unsigned produced = 0;

    ASSERT_LT(n, 15u);
    EXPECT_EQ(inflate_back_raw(raw, n, 0, decoded, &produced), Z_DATA_ERROR);
}

/* A distance that stays inside the bytes just written is valid on either path. */
TEST(inflate_back_distance, match_inside_output) {
    uint8_t raw[32];
    uint8_t decoded[64];
    unsigned n = craft_stored_then_match(raw, 1);
    unsigned produced = 0;

    EXPECT_EQ(inflate_back_raw(raw, n, 32, decoded, &produced), Z_STREAM_END);
    ASSERT_EQ(produced, 4u);
    EXPECT_EQ(decoded[0], 0xa5);
    EXPECT_EQ(decoded[1], 0xa5);
    EXPECT_EQ(decoded[2], 0xa5);
    EXPECT_EQ(decoded[3], 0xa5);
}

/* inflateBack() decodes into the caller's window, so inflate_fast() copies a match that reaches into the
 * previous pass of the window within that one buffer, and source and destination can overlap. Each stream is
 * one fixed Huffman block: 2^wbits + k literals, a match of length len at distance dist, then 40 literals. */
TEST(inflate_back_window, overlapping_window_copy) {
    struct {
        int wbits;
        unsigned k, dist, len;
    } const cases[] = {
        {8, 100, 200, 150}, {8, 100, 250, 100}, {8, 100, 256, 100}, {8, 3, 255, 10},
        {9, 10, 400, 258}, {15, 1000, 32700, 200}, {15, 1000, 32768, 258}
    };
    for (const auto &c : cases) {
        SCOPED_TRACE(testing::Message() << "wbits " << c.wbits << ", k " << c.k << ", dist " << c.dist
                                        << ", len " << c.len);
        const unsigned wsize = 1U << c.wbits;
        std::vector<uint8_t> comp, expect;
        uint64_t acc = 0;
        int nacc = 0;
        auto put = [&](uint32_t v, int n) {
            acc |= (uint64_t)v << nacc;
            nacc += n;
            for (; nacc >= 8; nacc -= 8, acc >>= 8)
                comp.push_back((uint8_t)acc);
        };
        auto huff = [&](uint32_t code, int n) {  /* Huffman codes go most significant bit first */
            for (int i = n - 1; i >= 0; i--)
                put((code >> i) & 1, 1);
        };
        auto lit = [&](uint8_t v) {
            if (v < 144)
                huff(0x30 + v, 8);
            else
                huff(0x190 + v - 144, 9);
            expect.push_back(v);
        };
        static const uint32_t lbase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                         67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint32_t dbase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
                                         769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

        put(1, 1);  /* last block */
        put(1, 2);  /* fixed codes */
        for (unsigned i = 0; i < wsize + c.k; i++)
            lit((uint8_t)(i * 7 + i / 251));
        int lc = 28;
        while (lbase[lc] > c.len)
            lc--;
        if (lc < 23)
            huff((uint32_t)lc + 1, 7);           /* codes 257..279 */
        else
            huff(0xc0 + (uint32_t)lc - 23, 8);   /* codes 280..285 */
        put(c.len - lbase[lc], lc < 8 || lc == 28 ? 0 : (lc - 4) / 4);
        int dc = 29;
        while (dbase[dc] > c.dist)
            dc--;
        huff((uint32_t)dc, 5);
        put(c.dist - dbase[dc], dc < 4 ? 0 : dc / 2 - 1);
        for (unsigned i = 0; i < c.len; i++)
            expect.push_back(expect[expect.size() - c.dist]);
        for (int i = 0; i < 40; i++)
            lit('A');
        huff(0, 7);  /* end of block */
        if (nacc)
            comp.push_back((uint8_t)acc);

        std::vector<uint8_t> window(wsize), out(expect.size());
        struct output_state out_state = {out.data(), out.size(), 0};
        PREFIX3(stream) strm = {};
        ASSERT_EQ(PREFIX(inflateBackInit)(&strm, c.wbits, window.data()), Z_OK);
        strm.next_in = comp.data();
        strm.avail_in = (uint32_t)comp.size();
        EXPECT_EQ(PREFIX(inflateBack)(&strm, pull_all, &strm, push_all, &out_state), Z_STREAM_END);
        EXPECT_EQ(PREFIX(inflateBackEnd)(&strm), Z_OK);
        ASSERT_EQ(out_state.pos, expect.size());
        EXPECT_EQ(memcmp(out.data(), expect.data(), expect.size()), 0);
    }
}
