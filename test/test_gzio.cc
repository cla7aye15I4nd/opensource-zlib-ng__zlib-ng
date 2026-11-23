/* test_gzio.cc - Test read/write of .gz files */

#include "zbuild.h"
#ifdef ZLIB_COMPAT
#  include "zlib.h"
#else
#  include "zlib-ng.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#ifndef _WIN32
#  include <fcntl.h>
#  include <unistd.h>
#endif

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

#if !defined(_WIN32) && !defined(NO_GZCOMPRESS) && !defined(TEST_STOCK_ZLIB)
namespace {

/* Helper managing a POSIX pipe whose write end can be set non-blocking and filled to EAGAIN. */
struct printf_pipe {
    int fds[2];
    size_t junk;

    printf_pipe() : junk(0) {
        fds[0] = -1;
        fds[1] = -1;
    }

    ~printf_pipe() {
        if (fds[0] >= 0)
            close(fds[0]);
        if (fds[1] >= 0)
            close(fds[1]);
    }

    bool open_nonblock_writer() {
        if (pipe(fds) != 0)
            return false;
        int flags = fcntl(fds[1], F_GETFL, 0);
        if (flags == -1 || fcntl(fds[1], F_SETFL, flags | O_NONBLOCK) == -1)
            return false;
        return true;
    }

    void fill_pipe(int write_fd) {
        uint8_t pad[1024];
        memset(pad, 'x', sizeof(pad));
        for (;;) {
            ssize_t written = write(write_fd, pad, sizeof(pad));
            if (written > 0) {
                junk += (size_t)written;
                continue;
            }
            if (written == -1 && errno == EINTR)
                continue;
            if (written == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            FAIL() << "unexpected write() error while filling pipe: errno=" << errno;
        }
        /* Top off any sub-1024-byte tail so the pipe has zero room left. */
        for (;;) {
            uint8_t one = 'x';
            ssize_t written = write(write_fd, &one, 1);
            if (written == 1) {
                junk++;
                continue;
            }
            if (written == -1 && errno == EINTR)
                continue;
            ASSERT_EQ(written, -1);
            ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
            break;
        }
    }

    void drain_pipe() {
        uint8_t sink[1024];
        while (junk > 0) {
            size_t want = junk < sizeof(sink) ? junk : sizeof(sink);
            ssize_t nread = read(fds[0], sink, want);
            if (nread > 0) {
                junk -= (size_t)nread;
                continue;
            }
            if (nread == -1 && errno == EINTR)
                continue;
            FAIL() << "unexpected read() result while draining pipe: nread=" << nread << " errno=" << errno;
        }
    }

    void read_available(std::vector<uint8_t> &out) {
        int rflags = fcntl(fds[0], F_GETFL, 0);
        ASSERT_NE(rflags, -1);
        ASSERT_NE(fcntl(fds[0], F_SETFL, rflags | O_NONBLOCK), -1);
        uint8_t tmp[1024];
        for (;;) {
            ssize_t nread = read(fds[0], tmp, sizeof(tmp));
            if (nread > 0) {
                out.insert(out.end(), tmp, tmp + nread);
                continue;
            }
            if (nread == -1 && errno == EINTR)
                continue;
            if (nread == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            FAIL() << "unexpected read() error: nread=" << nread << " errno=" << errno;
        }
        ASSERT_NE(fcntl(fds[0], F_SETFL, rflags), -1);
    }

    int detach_writer() {
        int write_fd = fds[1];
        fds[1] = -1;
        return write_fd;
    }
};

} // namespace

/* Verify that gzprintf() rolls back unwritten output on non-blocking stalls (Z_BUF_ERROR) and
 * produces an intact gzip stream after draining the pipe and closing. */
TEST(gzip, printf_after_failed_writes) {
    printf_pipe pipe_pair;
    ASSERT_TRUE(pipe_pair.open_nonblock_writer());

    int wfd = pipe_pair.detach_writer();
    gzFile file = PREFIX(gzdopen)(wfd, "wb0");
    if (file == NULL) {
        close(wfd);
        FAIL() << "gzdopen failed";
    }

    const size_t buf_size = 1024;
    const size_t chunk = buf_size - 64;
    ASSERT_EQ(PREFIX(gzbuffer)(file, (unsigned)buf_size), 0);

    /* Emit the gzip header into the pipe first so later Z_SYNC_FLUSH fills
       the output buffer completely before attempting write(). */
    std::string expected;
    std::vector<uint8_t> compressed;
    const char init_text[] = "init\n";
    ASSERT_EQ(PREFIX(gzprintf)(file, "%s", init_text), (int32_t)(sizeof(init_text) - 1));
    expected += init_text;
    ASSERT_EQ(PREFIX(gzflush)(file, Z_SYNC_FLUSH), Z_OK);
    pipe_pair.read_available(compressed);
    ASSERT_GT(compressed.size(), 10u);

    pipe_pair.fill_pipe(wfd);

    /* Queue more than one output buffer while the output buffer is empty, then force
       Z_SYNC_FLUSH to fill it and stall on EAGAIN. */
    for (int idx = 0; idx < 2; idx++) {
        std::string item(chunk, (char)('0' + idx));
        int32_t ret = PREFIX(gzprintf)(file, "%s", item.c_str());
        ASSERT_EQ(ret, (int32_t)chunk);
        expected += item;
    }
    EXPECT_EQ(PREFIX(gzflush)(file, Z_SYNC_FLUSH), Z_ERRNO);

    /* First chunk fits in the lower half of the input buffer without flushing. */
    std::string first(chunk, 'a');
    ASSERT_EQ(PREFIX(gzprintf)(file, "%s", first.c_str()), (int32_t)chunk);
    expected += first;

    /* Next chunk exceeds half the input buffer, stalls, and must roll back
       all formatted bytes cleanly with Z_BUF_ERROR. */
    std::string stalled_item(chunk, 'b');
    EXPECT_EQ(PREFIX(gzprintf)(file, "%s", stalled_item.c_str()), Z_BUF_ERROR);

    /* Because stalled_item was rolled back, a small gzprintf() still fits without flushing. */
    const char small_ok[] = "ok";
    ASSERT_EQ(PREFIX(gzprintf)(file, "%s", small_ok), (int32_t)(sizeof(small_ok) - 1));
    expected += small_ok;

    for (int idx = 0; idx < 4; idx++) {
        std::string retry(chunk, (char)('c' + idx));
        EXPECT_EQ(PREFIX(gzprintf)(file, "%s", retry.c_str()), Z_BUF_ERROR);
        if (idx == 1) {
            PREFIX(gzclearerr)(file);
        }
    }

    pipe_pair.drain_pipe();

    const char tail[] = "tail\n";
    int32_t tail_ret = PREFIX(gzprintf)(file, "%s", tail);
    if (tail_ret > 0) {
        EXPECT_EQ(tail_ret, (int32_t)(sizeof(tail) - 1));
        expected += tail;
    } else {
        FAIL() << "expected final gzprintf() after drain to succeed, got " << tail_ret;
    }

    EXPECT_EQ(PREFIX(gzclose)(file), Z_OK);

    uint8_t buf[4096];
    for (;;) {
        ssize_t nread = read(pipe_pair.fds[0], buf, sizeof(buf));
        if (nread > 0) {
            compressed.insert(compressed.end(), buf, buf + nread);
            continue;
        }
        if (nread == 0)
            break;
        if (errno == EINTR)
            continue;
        FAIL() << "read() from pipe failed: errno=" << errno;
    }

    PREFIX3(stream) strm;
    memset(&strm, 0, sizeof(strm));
    ASSERT_EQ(PREFIX(inflateInit2)(&strm, MAX_WBITS + 16), Z_OK);

    std::string decoded(expected.size() + 64, '\0');
    strm.next_in = compressed.data();
    strm.avail_in = (uint32_t)compressed.size();
    strm.next_out = (uint8_t *)&decoded[0];
    strm.avail_out = (uint32_t)decoded.size();

    int inf_ret = PREFIX(inflate)(&strm, Z_FINISH);
    EXPECT_EQ(inf_ret, Z_STREAM_END);
    EXPECT_EQ(strm.avail_in, 0u);
    decoded.resize(decoded.size() - strm.avail_out);
    EXPECT_EQ(PREFIX(inflateEnd)(&strm), Z_OK);

    EXPECT_EQ(decoded.size(), expected.size());
    EXPECT_EQ(decoded, expected);
}
#endif
