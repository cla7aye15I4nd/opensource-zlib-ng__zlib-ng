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

TEST(gzip, gets_pending_skip_after_eof) {
    const char *path = "seek_gets_eof_grow.txt";
    FILE *fp = fopen(path, "wb");
    ASSERT_NE(fp, nullptr);
    ASSERT_EQ(fwrite("abc", 1, 3, fp), 3U);
    ASSERT_EQ(fclose(fp), 0);

    gzFile file = PREFIX(gzopen)(path, "rb");
    ASSERT_NE(file, (gzFile)NULL);
#if !defined(ZLIB_COMPAT) || defined(Z_LARGE64)
    const z_off64_t off_max =
        (z_off64_t)((((uint64_t)1 << (sizeof(z_off64_t) * 8 - 2)) - 1) * 2 + 1);
#  ifdef ZLIB_COMPAT
    EXPECT_EQ(PREFIX4(gzseek)(file, off_max, SEEK_SET), off_max);
#  else
    EXPECT_EQ(PREFIX(gzseek)(file, off_max, SEEK_SET), off_max);
#  endif
    /* Force execution of the pending seek to EOF. */
    EXPECT_EQ(PREFIX(gzungetc)(-1, file), -1);
#  ifdef ZLIB_COMPAT
    EXPECT_EQ(PREFIX4(gztell)(file), 3);
#  else
    EXPECT_EQ(PREFIX(gztell)(file), 3);
#  endif

    /* Appending after EOF without gzclearerr() must not make gzgets() read pre-seek data. */
    fp = fopen(path, "ab");
    ASSERT_NE(fp, nullptr);
    ASSERT_EQ(fwrite("de\n", 1, 3, fp), 3U);
    ASSERT_EQ(fclose(fp), 0);

    char buf[16];
    EXPECT_EQ(PREFIX(gzgets)(file, buf, sizeof(buf)), nullptr);
#  ifdef ZLIB_COMPAT
    EXPECT_EQ(PREFIX4(gztell)(file), 3);
#  else
    EXPECT_EQ(PREFIX(gztell)(file), 3);
#  endif
#endif
    EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);
    remove(path);
}
