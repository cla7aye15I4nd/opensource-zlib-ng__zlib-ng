/* test_gzio.cc - Test read/write of .gz files */

#include "zbuild.h"
#ifdef ZLIB_COMPAT
#  include "zlib.h"
#else
#  include "zlib-ng.h"
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#ifndef _WIN32
#  include <unistd.h>
#else
#  include <io.h>
#endif
#include <string.h>
#include <vector>

#include <gtest/gtest.h>

#include "test_shared.h"

#define TESTFILE "foo.gz"

TEST(gzip, readwrite) {
#ifdef NO_GZCOMPRESS
    fprintf(stderr, "NO_GZCOMPRESS -- gz* functions cannot compress\n");
    GTEST_SKIP();
#else
    uint8_t uncompr[128];
    uint32_t compr_len, uncompr_len = sizeof(uncompr);
    int64_t pos;
    gzFile file;
    int err;

    /* Write gz file with test data */
    file = PREFIX(gzopen)(TESTFILE, "wb");
    ASSERT_TRUE(file != NULL);
    /* Write hello, hello! using gzputs and gzprintf */
    PREFIX(gzputc)(file, 'h');
    EXPECT_EQ(PREFIX(gzputs)(file, "ello"), 4);
    EXPECT_EQ(PREFIX(gzprintf)(file, ", %s!", "hello"), 8);
    /* Write string null-teriminator using gzseek */
    EXPECT_GE(PREFIX(gzseek)(file, 1L, SEEK_CUR), 0);
    /* Write hello, hello! using gzfwrite using best compression level */
    EXPECT_EQ(PREFIX(gzsetparams)(file, Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY), Z_OK);
    EXPECT_NE(PREFIX(gzfwrite)(hello, hello_len, 1, file), 0UL);
    /* Flush compressed bytes to file */
    EXPECT_EQ(PREFIX(gzflush)(file, Z_SYNC_FLUSH), Z_OK);
    compr_len = (uint32_t)PREFIX(gzoffset)(file);
    EXPECT_GE(compr_len, 0UL);
    PREFIX(gzclose)(file);

    /* Open gz file we previously wrote */
    file = PREFIX(gzopen)(TESTFILE, "rb");
    ASSERT_TRUE(file != NULL);

    /* Read uncompressed data - hello, hello! string twice */
    strcpy((char*)uncompr, "garbages");
    EXPECT_EQ(PREFIX(gzread)(file, uncompr, (unsigned)uncompr_len), (int)(hello_len + hello_len));
    EXPECT_STREQ((char*)uncompr, hello);

    /* Check position at the end of the gz file */
    EXPECT_EQ(PREFIX(gzeof)(file), 1);

    /* Seek backwards mid-string and check char reading with gzgetc and gzungetc */
    pos = PREFIX(gzseek)(file, -22L, SEEK_CUR);
    EXPECT_EQ(pos, 6);
    EXPECT_EQ(PREFIX(gztell)(file), pos);
    EXPECT_EQ(PREFIX(gzgetc)(file), ' ');
    EXPECT_EQ(PREFIX(gzungetc)(' ', file), ' ');
    /* Read first hello, hello! string with gzgets */
    strcpy((char*)uncompr, "garbages");
    PREFIX(gzgets)(file, (char*)uncompr, (int)uncompr_len);
    EXPECT_EQ(strlen((char*)uncompr), 7UL); /* " hello!" */
    EXPECT_STREQ((char*)uncompr, hello + 6);

    /* Seek to second hello, hello! string */
    pos = PREFIX(gzseek)(file, 14L, SEEK_SET);
    EXPECT_EQ(pos, 14);
    EXPECT_EQ(PREFIX(gztell)(file), pos);

    /* Check position not at end of file */
    EXPECT_EQ(PREFIX(gzeof)(file), 0);
    /* Read first hello, hello! string with gzfread */
    strcpy((char*)uncompr, "garbages");
    PREFIX(gzfread)(uncompr, uncompr_len, 1, file);
    EXPECT_STREQ((const char *)uncompr, hello);

    pos = PREFIX(gzoffset)(file);
    EXPECT_GE(pos, 0);
    EXPECT_EQ(pos, (compr_len + 10));

    /* Trigger an error and clear it with gzclearerr */
    PREFIX(gzfread)(uncompr, (size_t)-1, (size_t)-1, file);
    PREFIX(gzerror)(file, &err);
    EXPECT_NE(err, 0);

    PREFIX(gzclearerr)(file);
    PREFIX(gzerror)(file, &err);
    EXPECT_EQ(err, 0);

    PREFIX(gzclose)(file);

    EXPECT_EQ(PREFIX(gzclose)(NULL), Z_STREAM_ERROR);
#endif
}

TEST(gzip, setparams_invalid_parameters) {
#ifdef NO_GZCOMPRESS
    fprintf(stderr, "NO_GZCOMPRESS -- gz* functions cannot compress\n");
    GTEST_SKIP();
#else
    gzFile file = PREFIX(gzopen)(TESTFILE, "wb");
    ASSERT_TRUE(file != NULL);
    EXPECT_EQ(PREFIX(gzsetparams)(file, Z_BEST_COMPRESSION + 1, Z_DEFAULT_STRATEGY), Z_STREAM_ERROR);
    EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);

    file = PREFIX(gzopen)(TESTFILE, "wb");
    ASSERT_TRUE(file != NULL);
    EXPECT_EQ(PREFIX(gzsetparams)(file, Z_DEFAULT_COMPRESSION, Z_FIXED + 1), Z_STREAM_ERROR);
    EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);

    file = PREFIX(gzopen)(TESTFILE, "wb");
    ASSERT_TRUE(file != NULL);
    EXPECT_EQ(PREFIX(gzwrite)(file, "x", 1), 1);
    EXPECT_EQ(PREFIX(gzsetparams)(file, Z_BEST_COMPRESSION + 1, Z_DEFAULT_STRATEGY), Z_STREAM_ERROR);
    EXPECT_EQ(PREFIX(gzsetparams)(file, Z_DEFAULT_COMPRESSION, Z_FIXED + 1), Z_STREAM_ERROR);
    EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);
#endif
}

TEST(gzip, setparams_small_output_buffer) {
#ifdef NO_GZCOMPRESS
    fprintf(stderr, "NO_GZCOMPRESS -- gz* functions cannot compress\n");
    GTEST_SKIP();
#else
    std::vector<uint8_t> input(100000);
    std::vector<uint8_t> output(input.size());
    uint32_t random = 1;

    for (uint8_t &byte : input) {
        random = random * 1664525U + 1013904223U;
        byte = (uint8_t)(random >> 24);
    }

    gzFile file = PREFIX(gzopen)(TESTFILE, "wb1");
    ASSERT_TRUE(file != NULL);
    ASSERT_EQ(PREFIX(gzbuffer)(file, 9), 0);
    ASSERT_EQ(PREFIX(gzwrite)(file, input.data(), (unsigned)input.size()), (int)input.size());
    EXPECT_EQ(PREFIX(gzsetparams)(file, 9, Z_DEFAULT_STRATEGY), Z_OK);
    ASSERT_EQ(PREFIX(gzclose)(file), Z_OK);

    file = PREFIX(gzopen)(TESTFILE, "rb");
    ASSERT_TRUE(file != NULL);
    EXPECT_EQ(PREFIX(gzread)(file, output.data(), (unsigned)output.size()), (int)output.size());
    EXPECT_EQ(output, input);
    EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);
#endif
}

#ifdef __linux__
TEST(gzip, read_position_overflow) {
    const z_off64_t off_max =
        (z_off64_t)((((uint64_t)1 << (sizeof(z_off64_t) * 8 - 2)) - 1) * 2 + 1);
#  if !defined(ZLIB_COMPAT)
    typedef z_off64_t roff_t;
#    define R_GZSEEK PREFIX(gzseek)
#    define R_GZTELL PREFIX(gztell)
#  elif defined(Z_LARGE64)
    typedef z_off64_t roff_t;
#    define R_GZSEEK PREFIX4(gzseek)
#    define R_GZTELL PREFIX4(gztell)
#  else
    if (sizeof(z_off_t) != sizeof(z_off64_t))
        GTEST_SKIP() << "Requires 64-bit gzseek";
    typedef z_off_t roff_t;
#    define R_GZSEEK PREFIX(gzseek)
#    define R_GZTELL PREFIX(gztell)
#  endif

    /* 1. Public gzgetc() macro stops cleanly at off_max without overflowing pos. */
    gzFile f = PREFIX(gzopen)("/dev/zero", "rb");
    ASSERT_NE(f, (gzFile)NULL);
    EXPECT_EQ(PREFIX(gzgetc)(f), 0);
    if (R_GZSEEK(f, (roff_t)(off_max - 4), SEEK_SET) != (roff_t)(off_max - 4)) {
        PREFIX(gzclose)(f);
        GTEST_SKIP() << "/dev/zero does not allow raw seek";
    }
    for (int i = 0; i < 4; i++)
        EXPECT_EQ(PREFIX(gzgetc)(f), 0);
    EXPECT_EQ(R_GZTELL(f), (roff_t)off_max);
    EXPECT_EQ(PREFIX(gzgetc)(f), -1);
    int err = Z_OK;
    PREFIX(gzerror)(f, &err);
    EXPECT_EQ(err, Z_DATA_ERROR);
    EXPECT_EQ(R_GZTELL(f), (roff_t)off_max);
    EXPECT_EQ(PREFIX(gzclose)(f), Z_OK);

    /* 2. gzread() stops cleanly at off_max and reports Z_DATA_ERROR on extra data. */
    f = PREFIX(gzopen)("/dev/zero", "rb");
    ASSERT_NE(f, (gzFile)NULL);
    EXPECT_EQ(PREFIX(gzgetc)(f), 0);
    ASSERT_EQ(R_GZSEEK(f, (roff_t)(off_max - 4), SEEK_SET), (roff_t)(off_max - 4));
    uint8_t buf[8];
    EXPECT_EQ(PREFIX(gzread)(f, buf, 4), 4);
    PREFIX(gzerror)(f, &err);
    EXPECT_EQ(err, Z_OK);
    EXPECT_EQ(R_GZTELL(f), (roff_t)off_max);
    EXPECT_EQ(PREFIX(gzread)(f, buf, 1), -1);
    PREFIX(gzerror)(f, &err);
    EXPECT_EQ(err, Z_DATA_ERROR);
    EXPECT_EQ(PREFIX(gzclose)(f), Z_OK);

    /* 3. gzgets() stops cleanly at off_max as well. */
    f = PREFIX(gzopen)("/dev/zero", "rb");
    ASSERT_NE(f, (gzFile)NULL);
    EXPECT_EQ(PREFIX(gzgetc)(f), 0);
    ASSERT_EQ(R_GZSEEK(f, (roff_t)(off_max - 2), SEEK_SET), (roff_t)(off_max - 2));
    char lbuf[16];
    memset(lbuf, 'x', sizeof(lbuf));
    EXPECT_EQ(PREFIX(gzgets)(f, lbuf, sizeof(lbuf)), lbuf);
    EXPECT_EQ(lbuf[0], '\0');
    EXPECT_EQ(lbuf[1], '\0');
    EXPECT_EQ(lbuf[2], '\0');
    EXPECT_EQ(lbuf[3], 'x');
    EXPECT_EQ(R_GZTELL(f), (roff_t)off_max);
    EXPECT_EQ(PREFIX(gzgets)(f, lbuf, sizeof(lbuf)), nullptr);
    PREFIX(gzerror)(f, &err);
    EXPECT_EQ(err, Z_DATA_ERROR);
    EXPECT_EQ(PREFIX(gzclose)(f), Z_OK);
}
#endif
