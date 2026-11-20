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

TEST(gzip, direct_read_keeps_pending_seek) {
    const char *path = "seek_direct_pending.txt";
    FILE *fp = fopen(path, "wb");
    ASSERT_NE(fp, nullptr);
    ASSERT_EQ(fwrite("abcde", 1, 5, fp), 5U);
    ASSERT_EQ(fclose(fp), 0);

    gzFile file = PREFIX(gzopen)(path, "rb");
    ASSERT_NE(file, (gzFile)NULL);
    EXPECT_EQ(PREFIX(gzseek)(file, 3, SEEK_SET), 3);
    EXPECT_EQ(PREFIX(gzdirect)(file), 1);
    EXPECT_EQ(PREFIX(gzgetc)(file), 'd');
    EXPECT_EQ(PREFIX(gztell)(file), 4);
    EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);

    /* Seeking near the signed limit before gzdirect() must also consume buffered
       bytes so subsequent gzgetc() and gztell() do not overflow. */
    const z_off64_t off_max =
        (z_off64_t)((((uint64_t)1 << (sizeof(z_off64_t) * 8 - 2)) - 1) * 2 + 1);
#if !defined(ZLIB_COMPAT) || defined(Z_LARGE64)
    file = PREFIX(gzopen)(path, "rb");
    ASSERT_NE(file, (gzFile)NULL);
#  ifdef ZLIB_COMPAT
    EXPECT_EQ(PREFIX4(gzseek)(file, off_max, SEEK_SET), off_max);
    EXPECT_EQ(PREFIX(gzdirect)(file), 1);
    EXPECT_EQ(PREFIX(gzgetc)(file), -1);
    EXPECT_EQ(PREFIX4(gztell)(file), 5);
#  else
    EXPECT_EQ(PREFIX(gzseek)(file, off_max, SEEK_SET), off_max);
    EXPECT_EQ(PREFIX(gzdirect)(file), 1);
    EXPECT_EQ(PREFIX(gzgetc)(file), -1);
    EXPECT_EQ(PREFIX(gztell)(file), 5);
#  endif
    EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);
#endif
    remove(path);
}
