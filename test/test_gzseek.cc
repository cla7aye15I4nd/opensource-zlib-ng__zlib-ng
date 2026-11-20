/* test_gzseek.cc - Test seeking of .gz files */

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

#define TESTFILE "seek.gz"
#define MARK "SECRETSECRET"   /* 12 bytes */

TEST(gzip, seek) {
#ifdef NO_GZCOMPRESS
    fprintf(stderr, "NO_GZCOMPRESS -- gz* functions cannot compress\n");
    GTEST_SKIP();
#else
    uint8_t buffer[6000];
    gzFile file;
    int64_t pos;

    void **p = (void**)malloc(sizeof(void *) * 40);
    ASSERT_TRUE(p != NULL);
    for (int i = 0; i < 40; i++) {
        p[i] = malloc(8192);
        if (!p[i]) continue;
        for (size_t o = 0; o + 12 <= 8192; o += 12) memcpy((char *)p[i] + o, MARK, 12);
    }
    for (int i = 0; i < 40; i++) free(p[i]);
    free(p);

    /* Write gz file with zeroes */
    file = PREFIX(gzopen)(TESTFILE, "wb");
    ASSERT_TRUE(file != NULL);
    PREFIX(gzbuffer)(file, 8192);
    pos = PREFIX(gzseek)(file, 6000, SEEK_SET);
    EXPECT_EQ(pos, 6000);
    PREFIX(gzclose)(file);

    /* Open gz file we previously wrote */
    file = PREFIX(gzopen)(TESTFILE, "rb");
    ASSERT_TRUE(file != NULL);

    size_t total = 0, nonzero = 0;
    int hits = 0, n;
    while ((n = PREFIX(gzread)(file, buffer, sizeof(buffer))) > 0) {
        total += n;
        for (int i = 0; i < n; i++) if (buffer[i]) nonzero++;
        for (int i = 0; i + 12 <= n; i++) if (!memcmp(buffer + i, MARK, 12)) hits++;
    }
    EXPECT_EQ(nonzero, 0);
    EXPECT_EQ(hits, 0);
    EXPECT_EQ(total, 6000);

    PREFIX(gzclose)(file);
#endif
}

#if !defined(ZLIB_COMPAT)
typedef z_off64_t seek_test_off_t;
#  define SEEK_TEST_GZSEEK PREFIX(gzseek)
#  define SEEK_TEST_GZTELL PREFIX(gztell)
#elif defined(Z_LARGE64)
typedef z_off64_t seek_test_off_t;
#  define SEEK_TEST_GZSEEK PREFIX4(gzseek)
#  define SEEK_TEST_GZTELL PREFIX4(gztell)
#else
typedef z_off_t seek_test_off_t;
#  define SEEK_TEST_GZSEEK PREFIX(gzseek)
#  define SEEK_TEST_GZTELL PREFIX(gztell)
#endif

TEST(gzip, seek_offset_overflow) {
    const seek_test_off_t off_max =
        (seek_test_off_t)((((uint64_t)1 << (sizeof(seek_test_off_t) * 8 - 2)) - 1) * 2 + 1);
    const seek_test_off_t off_min = -off_max - 1;

    FILE *fp = fopen("seek_overflow_raw.txt", "wb");
    ASSERT_NE(fp, nullptr);
    ASSERT_EQ(fwrite("abcdef", 1, 6, fp), 6U);
    ASSERT_EQ(fclose(fp), 0);

    gzFile rfile = PREFIX(gzopen)("seek_overflow_raw.txt", "rb");
    ASSERT_NE(rfile, (gzFile)NULL);
    EXPECT_EQ(SEEK_TEST_GZSEEK(rfile, off_min, SEEK_SET), (seek_test_off_t)-1);
    EXPECT_EQ(SEEK_TEST_GZSEEK(rfile, off_max, SEEK_SET), off_max);
    EXPECT_EQ(SEEK_TEST_GZSEEK(rfile, 1, SEEK_CUR), (seek_test_off_t)-1);
    EXPECT_EQ(SEEK_TEST_GZTELL(rfile), off_max);
    EXPECT_EQ(PREFIX(gzclose)(rfile), Z_OK);
    remove("seek_overflow_raw.txt");

#ifndef NO_GZCOMPRESS
    gzFile wfile = PREFIX(gzopen)("seek_overflow_w.gz", "wb");
    ASSERT_NE(wfile, (gzFile)NULL);
    EXPECT_EQ(SEEK_TEST_GZSEEK(wfile, 1, SEEK_SET), (seek_test_off_t)1);
    EXPECT_EQ(SEEK_TEST_GZSEEK(wfile, off_max, SEEK_CUR), (seek_test_off_t)-1);
    EXPECT_EQ(SEEK_TEST_GZTELL(wfile), (seek_test_off_t)1);
    EXPECT_EQ(SEEK_TEST_GZSEEK(wfile, off_min, SEEK_CUR), (seek_test_off_t)-1);
    EXPECT_EQ(SEEK_TEST_GZTELL(wfile), (seek_test_off_t)1);
    EXPECT_EQ(PREFIX(gzwrite)(wfile, "a", 1), 1);
    EXPECT_EQ(SEEK_TEST_GZSEEK(wfile, 100, SEEK_CUR), (seek_test_off_t)102);
    EXPECT_EQ(SEEK_TEST_GZSEEK(wfile, -200, SEEK_CUR), (seek_test_off_t)-1);
    EXPECT_EQ(SEEK_TEST_GZTELL(wfile), (seek_test_off_t)102);
    /* Cancel the pending skip before gzclose() so nothing is zero filled. */
    EXPECT_EQ(PREFIX(gzseek)(wfile, 2, SEEK_SET), 2);
    EXPECT_EQ(PREFIX(gzclose)(wfile), Z_OK);
    remove("seek_overflow_w.gz");
#endif
}

TEST(gzip, seek_from_negative_read_position) {
    const seek_test_off_t off_max =
        (seek_test_off_t)((((uint64_t)1 << (sizeof(seek_test_off_t) * 8 - 2)) - 1) * 2 + 1);

    FILE *fp = fopen("seek_neg_raw.txt", "wb");
    ASSERT_NE(fp, nullptr);
    ASSERT_EQ(fwrite("ab", 1, 2, fp), 2U);
    ASSERT_EQ(fclose(fp), 0);

    gzFile rfile = PREFIX(gzopen)("seek_neg_raw.txt", "rb");
    ASSERT_NE(rfile, (gzFile)NULL);
    EXPECT_EQ(PREFIX(gzungetc)('x', rfile), 'x');
    EXPECT_EQ(SEEK_TEST_GZTELL(rfile), (seek_test_off_t)-1);
    /* Seeking to zero via either whence discards pushback cleanly. */
    EXPECT_EQ(SEEK_TEST_GZSEEK(rfile, 0, SEEK_SET), (seek_test_off_t)0);
    EXPECT_EQ(PREFIX(gzungetc)('x', rfile), 'x');
    /* At pos = -1, seeking by 0 is still before start of file, and off_max stays valid. */
    EXPECT_EQ(SEEK_TEST_GZSEEK(rfile, 0, SEEK_CUR), (seek_test_off_t)-1);
    EXPECT_EQ(SEEK_TEST_GZTELL(rfile), (seek_test_off_t)-1);
    EXPECT_EQ(SEEK_TEST_GZSEEK(rfile, off_max, SEEK_SET), off_max);
    EXPECT_EQ(PREFIX(gzclose)(rfile), Z_OK);
    remove("seek_neg_raw.txt");

#ifndef NO_GZCOMPRESS
    gzFile wfile = PREFIX(gzopen)("seek_neg_gz.gz", "wb");
    ASSERT_NE(wfile, (gzFile)NULL);
    EXPECT_EQ(PREFIX(gzwrite)(wfile, "ab", 2), 2);
    EXPECT_EQ(PREFIX(gzclose)(wfile), Z_OK);

    gzFile zfile = PREFIX(gzopen)("seek_neg_gz.gz", "rb");
    ASSERT_NE(zfile, (gzFile)NULL);
    EXPECT_EQ(PREFIX(gzungetc)('x', zfile), 'x');
    EXPECT_EQ(SEEK_TEST_GZSEEK(zfile, 1, SEEK_CUR), (seek_test_off_t)0);
    EXPECT_EQ(PREFIX(gzgetc)(zfile), 'a');
    EXPECT_EQ(PREFIX(gzrewind)(zfile), 0);
    EXPECT_EQ(PREFIX(gzungetc)('x', zfile), 'x');
    EXPECT_EQ(SEEK_TEST_GZSEEK(zfile, off_max, SEEK_SET), off_max);
    EXPECT_EQ(PREFIX(gzclose)(zfile), Z_OK);
    remove("seek_neg_gz.gz");
#endif
}
