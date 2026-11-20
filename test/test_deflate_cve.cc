/* test_deflate_cve.cc - Test deflate() round-trips the sample inputs from past CVEs */

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

#include <gtest/gtest.h>

#include "test_data_p.h"

/* CVE-2018-25032 - Memory corruption when compressing if the input has many distant matches.
 * Sample inputs by Tavis Ormandy (Z_FIXED) and Eric Biggers (default strategy). */
struct cve_2018_25032_param {
    const char *file;
    size_t size;
    int32_t strategy;
    int32_t level;
    const char *name;
};

static const cve_2018_25032_param cve_2018_25032_params[] = {
    { "../CVE-2018-25032/fixed.txt",   32768, Z_FIXED,            6, "fixed_level_6" },
    { "../CVE-2018-25032/fixed.txt",   32768, Z_FIXED,            1, "fixed_level_1" },
    { "../CVE-2018-25032/fixed.txt",   32768, Z_FIXED,            2, "fixed_level_2" },
    { "../CVE-2018-25032/default.txt", 48192, Z_DEFAULT_STRATEGY, 6, "default_level_6" },
    { "../CVE-2018-25032/default.txt", 48192, Z_DEFAULT_STRATEGY, 1, "default_level_1" },
    { "../CVE-2018-25032/default.txt", 48192, Z_DEFAULT_STRATEGY, 2, "default_level_2" },
};

class cve_2018_25032 : public ::testing::TestWithParam<cve_2018_25032_param> {
protected:
    uint8_t *src = nullptr;
    uint8_t *compr = nullptr;
    uint8_t *uncompr = nullptr;

    void TearDown() override {
        free(src);
        free(compr);
        free(uncompr);
    }
};

TEST_P(cve_2018_25032, roundtrip) {
    const cve_2018_25032_param &p = GetParam();
    PREFIX3(stream) strm;
    int err;

    src = get_data_file(p.file, p.size);
    ASSERT_NE(src, nullptr) << "cannot read " << p.file;

    memset(&strm, 0, sizeof(strm));
    err = PREFIX(deflateInit2)(&strm, p.level, Z_DEFLATED, -MAX_WBITS, 1, p.strategy);
    ASSERT_EQ(err, Z_OK);

    size_t compr_size = PREFIX(deflateBound)(&strm, (unsigned long)p.size);
    compr = (uint8_t *)malloc(compr_size);
    ASSERT_NE(compr, nullptr);

    /* The whole input goes to a single Z_FINISH call, as in the original reproducer */
    strm.next_in = src;
    strm.avail_in = (uint32_t)p.size;
    strm.next_out = compr;
    strm.avail_out = (uint32_t)compr_size;
    err = PREFIX(deflate)(&strm, Z_FINISH);
    EXPECT_EQ(err, Z_STREAM_END);
    size_t compr_len = (size_t)strm.total_out;
    err = PREFIX(deflateEnd)(&strm);
    EXPECT_EQ(err, Z_OK);

    uncompr = (uint8_t *)malloc(p.size);
    ASSERT_NE(uncompr, nullptr);

    memset(&strm, 0, sizeof(strm));
    err = PREFIX(inflateInit2)(&strm, -MAX_WBITS);
    ASSERT_EQ(err, Z_OK);

    strm.next_in = compr;
    strm.avail_in = (uint32_t)compr_len;
    strm.next_out = uncompr;
    strm.avail_out = (uint32_t)p.size;
    err = PREFIX(inflate)(&strm, Z_FINISH);
    EXPECT_EQ(err, Z_STREAM_END);
    EXPECT_EQ(strm.total_out, p.size);
    err = PREFIX(inflateEnd)(&strm);
    EXPECT_EQ(err, Z_OK);

    EXPECT_EQ(memcmp(uncompr, src, p.size), 0);
}

INSTANTIATE_TEST_SUITE_P(deflate, cve_2018_25032, testing::ValuesIn(cve_2018_25032_params),
    [](const ::testing::TestParamInfo<cve_2018_25032_param> &info) {
        return std::string(info.param.name);
    });

TEST(deflate_quick, fixed_block_across_large_input) {
    if (getenv("ZLIB_NG_TEST_LARGE_FIXED_BLOCK") == NULL)
        GTEST_SKIP() << "Processes more than 2 GiB of input";
    static uint8_t input[1 << 20], output[1 << 20];
    for (size_t i = 0; i < sizeof(input); i++)
        input[i] = (uint8_t)(((i * 2654435761U) >> 24) & 3);

    PREFIX3(stream) stream = {};
    ASSERT_EQ(PREFIX(deflateInit2)(&stream, 1, Z_DEFLATED, MAX_WBITS, 8, Z_FIXED), Z_OK);
    for (unsigned chunk = 0; chunk < 2052; chunk++) {
        stream.next_in = input;
        stream.avail_in = sizeof(input);
        do {
            stream.next_out = output;
            stream.avail_out = sizeof(output);
            ASSERT_EQ(PREFIX(deflate)(&stream, Z_NO_FLUSH), Z_OK);
        } while (stream.avail_in);
    }
    int ret;
    do {
        stream.next_out = output;
        stream.avail_out = sizeof(output);
        ret = PREFIX(deflate)(&stream, Z_FINISH);
    } while (ret == Z_OK);
    EXPECT_EQ(ret, Z_STREAM_END);
    EXPECT_EQ(stream.total_in, UINT64_C(2151677952));
    EXPECT_EQ(PREFIX(deflateEnd)(&stream), Z_OK);
}
