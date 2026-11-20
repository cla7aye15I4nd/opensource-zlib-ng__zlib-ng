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
#include <errno.h>
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

#ifndef _WIN32
TEST(gzip, write_pending_seek_error_preserves_tell) {
    const z_off64_t off_max =
        (z_off64_t)((((uint64_t)1 << (sizeof(z_off64_t) * 8 - 2)) - 1) * 2 + 1);
#  if !defined(ZLIB_COMPAT)
    typedef z_off64_t woff_t;
#    define W_GZSEEK PREFIX(gzseek)
#    define W_GZTELL PREFIX(gztell)
#  elif defined(Z_LARGE64)
    typedef z_off64_t woff_t;
#    define W_GZSEEK PREFIX4(gzseek)
#    define W_GZTELL PREFIX4(gztell)
#  else
    if (sizeof(z_off_t) != sizeof(z_off64_t))
        GTEST_SKIP() << "Requires 64-bit gzseek";
    typedef z_off_t woff_t;
#    define W_GZSEEK PREFIX(gzseek)
#    define W_GZTELL PREFIX(gztell)
#  endif

    int fd = open("/dev/null", O_RDONLY);
    ASSERT_GE(fd, 0);
    gzFile file = PREFIX(gzdopen)(fd, "wbT");
    if (file == NULL)
        close(fd);
    ASSERT_NE(file, (gzFile)NULL);

    ASSERT_EQ(W_GZSEEK(file, (woff_t)off_max, SEEK_SET), (woff_t)off_max);
    EXPECT_EQ(PREFIX(gzwrite)(file, "x", 1), 0);
    EXPECT_EQ(W_GZTELL(file), (woff_t)off_max);

    PREFIX(gzclearerr)(file);
    EXPECT_EQ(PREFIX(gzputc)(file, 'y'), -1);
    EXPECT_EQ(W_GZTELL(file), (woff_t)off_max);
    EXPECT_EQ(PREFIX(gzclose)(file), Z_ERRNO);
}

TEST(gzip, write_pending_seek_eagain_retry) {
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    int flags = fcntl(fds[1], F_GETFL);
    ASSERT_NE(flags, -1);
    ASSERT_EQ(fcntl(fds[1], F_SETFL, flags | O_NONBLOCK), 0);

    /* Fill the pipe write buffer so the first gz_zero() write hits EAGAIN. */
    uint8_t fill[4096];
    memset(fill, 'F', sizeof(fill));
    size_t prefill = 0;
    for (;;) {
        ssize_t n = write(fds[1], fill, sizeof(fill));
        if (n > 0) {
            prefill += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        FAIL() << "Unexpected pipe prefill failure: errno=" << errno;
    }

    gzFile file = PREFIX(gzdopen)(fds[1], "wbT");
    if (file == NULL) {
        close(fds[0]);
        close(fds[1]);
        FAIL() << "gzdopen failed";
    }
    EXPECT_EQ(PREFIX(gzbuffer)(file, 4096), Z_OK);
    EXPECT_EQ(PREFIX(gzseek)(file, 10000, SEEK_SET), 10000);
    EXPECT_EQ(PREFIX(gzwrite)(file, "abc", 3), 0);
    int err = Z_OK;
    PREFIX(gzerror)(file, &err);
    EXPECT_EQ(err, Z_ERRNO);
    EXPECT_EQ(PREFIX(gztell)(file), 10000);

    /* Drain the pipe, clear the error, and retry writing after the seek. */
    size_t drained = 0;
    while (drained < prefill) {
        ssize_t n = read(fds[0], fill, sizeof(fill));
        ASSERT_GT(n, 0);
        drained += (size_t)n;
    }
    ASSERT_EQ(fcntl(fds[1], F_SETFL, flags), 0);
    PREFIX(gzclearerr)(file);

    EXPECT_EQ(PREFIX(gzwrite)(file, "abc", 3), 3);
    EXPECT_EQ(PREFIX(gztell)(file), 10003);
    EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);

    std::vector<uint8_t> got;
    for (;;) {
        ssize_t n = read(fds[0], fill, sizeof(fill));
        if (n == 0)
            break;
        ASSERT_GT(n, 0);
        got.insert(got.end(), fill, fill + n);
    }
    close(fds[0]);

    ASSERT_EQ(got.size(), 10003U);
    for (size_t i = 0; i < 10000; i++)
        EXPECT_EQ(got[i], 0);
    EXPECT_EQ(memcmp(got.data() + 10000, "abc", 3), 0);
}
#endif
