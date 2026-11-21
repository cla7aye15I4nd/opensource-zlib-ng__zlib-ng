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

/* gzdirect() after a forward gzseek() on transparent input must consume
 * buffered bytes covered by the pending skip so gzgetc() returns the byte
 * at the seek target, and must not touch x.next when nothing is buffered. */
TEST(gzip, seek_then_direct) {
    const char *path = "seek_direct.txt";
    FILE *fp = fopen(path, "wb");
    ASSERT_TRUE(fp != NULL);
    ASSERT_EQ(fwrite("abcde", 1, 5, fp), 5U);
    ASSERT_EQ(fclose(fp), 0);

    gzFile file = PREFIX(gzopen)(path, "rb");
    ASSERT_TRUE(file != NULL);
    EXPECT_EQ(PREFIX(gzseek)(file, 3, SEEK_SET), 3);
    EXPECT_EQ(PREFIX(gzdirect)(file), 1);
    EXPECT_EQ(PREFIX(gzgetc)(file), 'd');
    EXPECT_EQ(PREFIX(gztell)(file), 4);
    EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);

    file = PREFIX(gzopen)(path, "rb");
    ASSERT_TRUE(file != NULL);
    EXPECT_EQ(PREFIX(gzseek)(file, 100, SEEK_SET), 100);
    EXPECT_EQ(PREFIX(gzdirect)(file), 1);
    EXPECT_EQ(PREFIX(gzgetc)(file), -1);
    EXPECT_EQ(PREFIX(gztell)(file), 5);
    EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);

    typedef decltype(PREFIX(gzseek)((gzFile)NULL, 0, SEEK_SET)) off_type;
    if (sizeof(off_type) == sizeof(z_off64_t)) {
        const off_type off_max =
            (off_type)(((uint64_t)1 << (sizeof(off_type) * 8 - 1)) - 1);
        file = PREFIX(gzopen)(path, "rb");
        ASSERT_TRUE(file != NULL);
        EXPECT_EQ(PREFIX(gzseek)(file, off_max, SEEK_SET), off_max);
        EXPECT_EQ(PREFIX(gzdirect)(file), 1);
        EXPECT_EQ(PREFIX(gzgetc)(file), -1);
        EXPECT_EQ(PREFIX(gztell)(file), 5);
        EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);
    }

    /* Empty file: gz_look() leaves x.have == 0 and x.next unset. */
    fp = fopen(path, "wb");
    ASSERT_TRUE(fp != NULL);
    ASSERT_EQ(fclose(fp), 0);
    file = PREFIX(gzopen)(path, "rb");
    ASSERT_TRUE(file != NULL);
    EXPECT_EQ(PREFIX(gzdirect)(file), 1);
    EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);

    remove(path);
}
