/* gzwrite.c -- zlib functions for writing gzip files
 * Copyright (C) 2004-2026 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include "zbuild.h"
#include "zutil_p.h"
#include <stdarg.h>
#include "gzguts.h"

/* Local functions */
static int gz_write_init(gz_state *);
static int gz_comp(gz_state *, int);
static int gz_zero(gz_state *);
static size_t gz_write(gz_state *, void const *, size_t);

/* Initialize state for writing a gzip file.  Mark initialization by setting
   state->size to non-zero.  Return -1 on a memory allocation failure, or 0 on
   success. */
static int gz_write_init(gz_state *state) {
    PREFIX3(stream) *strm = &(state->strm);

    /* Allocate gz buffers */
    if (gz_buffer_alloc(state) != 0) {
        PREFIX(gz_error)(state, Z_MEM_ERROR, "out of memory");
        return -1;
    }

    /* only need deflate state if compressing */
    if (!state->direct) {
        /* allocate deflate memory, set up for gzip compression */
        int ret = PREFIX(deflateInit2)(strm, state->level, Z_DEFLATED, MAX_WBITS + 16, DEF_MEM_LEVEL, state->strategy);
        if (ret != Z_OK) {
            gz_buffer_free(state);
            if (ret == Z_MEM_ERROR) {
                PREFIX(gz_error)(state, Z_MEM_ERROR, "out of memory");
            } else {
                PREFIX(gz_error)(state, Z_STREAM_ERROR, "invalid compression parameters");
            }
            return -1;
        }
        strm->next_in = NULL;

        /* initialize write buffer */
        strm->avail_out = state->size;
        strm->next_out = state->out;
        state->x.next = strm->next_out;
    }
    return 0;
}

/* Compress whatever is at avail_in and next_in and write to the output file.
   Return -1 if there is an error writing to the output file or if gz_write_init()
   fails to allocate memory, otherwise 0.  flush is assumed to be a valid
   deflate() flush value.  If flush is Z_FINISH, then the deflate() state is
   reset to start a new gzip stream.  If gz->direct is true, then simply write
   to the output file without compressing, and ignore flush. */
static int gz_comp(gz_state *state, int flush) {
    int ret;
    ssize_t got;
    unsigned have, put, max = ((unsigned)-1 >> 2) + 1;
    PREFIX3(stream) *strm = &(state->strm);

    /* allocate memory if this is the first time through */
    if (state->size == 0 && gz_write_init(state) == -1)
        return -1;

    /* write directly if requested */
    if (state->direct) {
        while (strm->avail_in) {
            errno = 0;
            state->again = 0;
            put = strm->avail_in > max ? max : strm->avail_in;
            got = write(state->fd, strm->next_in, put);
            if (got < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    state->again = 1;
                PREFIX(gz_error)(state, Z_ERRNO, zstrerror());
                return -1;
            }
            strm->avail_in -= (unsigned)got;
            strm->next_in += got;
        }
        return 0;
    }

    /* check for a pending reset */
    if (state->reset) {
        /* don't start a new gzip member unless there is data to write and
           we're not flushing */
        if (strm->avail_in == 0 && flush == Z_NO_FLUSH)
            return 0;
        PREFIX(deflateReset)(strm);
        state->reset = 0;
    }

    /* run deflate() on provided input until it produces no more output */
    ret = Z_OK;
    do {
        /* write out current buffer contents if full, or if flushing, but if
           doing Z_FINISH then don't write until we get to Z_STREAM_END */
        if (strm->avail_out == 0 || (flush != Z_NO_FLUSH && (flush != Z_FINISH || ret == Z_STREAM_END))) {
            while (strm->next_out > state->x.next) {
                errno = 0;
                state->again = 0;
                put = strm->next_out - state->x.next > (int)max ? max : (unsigned)(strm->next_out - state->x.next);
                got = write(state->fd, state->x.next, put);
                if (got < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        state->again = 1;
                    PREFIX(gz_error)(state, Z_ERRNO, zstrerror());
                    return -1;
                }
                state->x.next += got;
            }
            if (strm->avail_out == 0) {
                strm->avail_out = state->size;
                strm->next_out = state->out;
                state->x.next = state->out;
            }
        }

        /* compress */
        have = strm->avail_out;
        ret = PREFIX(deflate)(strm, flush);
        if (ret == Z_STREAM_ERROR) {
            PREFIX(gz_error)(state, Z_STREAM_ERROR, "internal error: deflate stream corrupt");
            return -1;
        }
        have -= strm->avail_out;
    } while (have);

    /* if that completed a deflate stream, allow another to start */
    if (flush == Z_FINISH)
        state->reset = 1;
    /* all done, no errors */
    return 0;
}

/* Compress state->skip (> 0) zeros to output.  Return -1 on a write error or
   memory allocation failure by gz_comp(), or 0 on success. state->skip is
   updated with the number of successfully written zeros, in case there is a
   stall on a non-blocking write destination. */
static int gz_zero(gz_state *state) {
    int first, ret;
    unsigned n;
    PREFIX3(stream) *strm = &(state->strm);

    /* allocate memory if this is the first time through -- the input buffer
       must exist before it is zeroed below */
    if (state->size == 0 && gz_write_init(state) == -1)
        return -1;

    /* consume whatever's left in the input buffer */
    if (strm->avail_in && gz_comp(state, Z_NO_FLUSH) == -1)
        return -1;

    /* compress state->skip zeros */
    first = 1;
    do {
        n = GT_OFF(state->size) || (z_off64_t)state->size > state->skip ?
            (unsigned)state->skip : state->size;
        if (first) {
            memset(state->in, 0, n);
            first = 0;
        }
        strm->avail_in = n;
        strm->next_in = state->in;
        ret = gz_comp(state, Z_NO_FLUSH);
        n -= strm->avail_in;
        state->x.pos += n;
        state->skip -= n;
        if (ret == -1)
            return -1;
    } while (state->skip);
    return 0;
}

/* Write len bytes from buf to file.  Return the number of bytes written.  If
   the returned value is less than len, then there was an error. If the error
   was a non-blocking stall, then the number of bytes consumed is returned.
   For any other error, 0 is returned. */
static size_t gz_write(gz_state *state, void const *buf, size_t len) {
    size_t put = len;
    int ret;

    /* if len is zero, avoid unnecessary operations */
    if (len == 0)
        return 0;

    /* allocate memory if this is the first time through */
    if (state->size == 0 && gz_write_init(state) == -1)
        return 0;

    /* check for seek request */
    if (state->skip && gz_zero(state) == -1)
        return 0;

    /* for small len, copy to input buffer, otherwise compress directly */
    if (len < state->size) {
        /* copy to input buffer, compress when full */
        for (;;) {
            unsigned have, copy;

            if (state->strm.avail_in == 0)
                state->strm.next_in = state->in;
            have = (unsigned)((state->strm.next_in + state->strm.avail_in) -
                              state->in);
            copy = state->size - have;
            if (copy > len)
                copy = (unsigned)len;
            memcpy(state->in + have, buf, copy);
            state->strm.avail_in += copy;
            state->x.pos += copy;
            buf = (const char *)buf + copy;
            len -= copy;
            if (len == 0)
                break;
            if (gz_comp(state, Z_NO_FLUSH) == -1)
                return state->again ? put - len : 0;
        }
    } else {
        /* consume whatever's left in the input buffer */
        if (state->strm.avail_in && gz_comp(state, Z_NO_FLUSH) == -1)
            return 0;

        /* directly compress user buffer to file */
        state->strm.next_in = (z_const unsigned char *) buf;
        do {
            unsigned n = (unsigned)-1;
            if (n > len)
                n = (unsigned)len;
            state->strm.avail_in = n;
            ret = gz_comp(state, Z_NO_FLUSH);
            n -= state->strm.avail_in;
            state->x.pos += n;
            len -= n;
            if (ret == -1)
                return state->again ? put - len : 0;
        } while (len);
    }

    /* input was all buffered or compressed */
    return put;
}

/* -- see zlib.h -- */
z_int32_t Z_EXPORT PREFIX(gzwrite)(gzFile file, void const *buf, z_uint32_t len) {
    gz_state *state;

    /* get internal structure */
    if (file == NULL)
        return 0;
    /* reject a NULL buffer with nonzero length before it is read from */
    if (buf == NULL && len != 0)
        return 0;
    state = (gz_state *)file;

    /* check that we're writing and that there's no (serious) error */
    if (state->mode != GZ_WRITE || (state->err != Z_OK && !state->again))
        return 0;
    PREFIX(gz_error)(state, Z_OK, NULL);

    /* since an int is returned, make sure len fits in one, otherwise return
       with an error (this avoids a flaw in the interface) */
    if ((z_int32_t)len < 0) {
        PREFIX(gz_error)(state, Z_DATA_ERROR, "requested length does not fit in int");
        return 0;
    }

    /* write len bytes from buf (the return value will fit in an int) */
    return (z_int32_t)gz_write(state, buf, len);
}

/* -- see zlib.h -- */
size_t Z_EXPORT PREFIX(gzfwrite)(void const *buf, size_t size, size_t nitems, gzFile file) {
    size_t len;
    gz_state *state;

    /* Exit early if size is zero, also prevents potential division by zero */
    if (size == 0)
        return 0;

    /* get internal structure */
    if (file == NULL)
        return 0;
    state = (gz_state *)file;

    /* check that we're writing and that there's no (serious) error */
    if (state->mode != GZ_WRITE || (state->err != Z_OK && !state->again))
        return 0;
    PREFIX(gz_error)(state, Z_OK, NULL);

    /* compute bytes to read -- error on overflow */
    len = nitems * size;
    if (len / size != nitems) {
        PREFIX(gz_error)(state, Z_STREAM_ERROR, "request does not fit in a size_t");
        return 0;
    }

    /* write len bytes to buf, return the number of full items written */
    return len ? gz_write(state, buf, len) / size : 0;
}

/* -- see zlib.h -- */
z_int32_t Z_EXPORT PREFIX(gzputc)(gzFile file, z_int32_t c) {
    unsigned have;
    unsigned char buf[1];
    gz_state *state;
    PREFIX3(stream) *strm;

    /* get internal structure */
    if (file == NULL)
        return -1;
    state = (gz_state *)file;
    strm = &(state->strm);

    /* check that we're writing and that there's no (serious) error */
    if (state->mode != GZ_WRITE || (state->err != Z_OK && !state->again))
        return -1;
    PREFIX(gz_error)(state, Z_OK, NULL);

    /* check for seek request */
    if (state->skip && gz_zero(state) == -1)
        return -1;

    /* try writing to input buffer for speed (state->size == 0 if buffer not
       initialized) */
    if (state->size) {
        if (strm->avail_in == 0)
            strm->next_in = state->in;
        have = (unsigned)((strm->next_in + strm->avail_in) - state->in);
        if (have < state->size) {
            state->in[have] = (unsigned char)c;
            strm->avail_in++;
            state->x.pos++;
            return c & 0xff;
        }
    }

    /* no room in buffer or not initialized, use gz_write() */
    buf[0] = (unsigned char)c;
    if (gz_write(state, buf, 1) != 1)
        return -1;
    return c & 0xff;
}

/* -- see zlib.h -- */
z_int32_t Z_EXPORT PREFIX(gzputs)(gzFile file, const char *s) {
    size_t len, put;
    gz_state *state;

    /* get internal structure */
    if (file == NULL || s == NULL)
        return -1;
    state = (gz_state *)file;

    /* check that we're writing and that there's no (serious) error */
    if (state->mode != GZ_WRITE || (state->err != Z_OK && !state->again))
        return -1;
    PREFIX(gz_error)(state, Z_OK, NULL);

    /* write string */
    len = strlen(s);
    if ((int)len < 0 || (unsigned)len != len) {
        PREFIX(gz_error)(state, Z_STREAM_ERROR, "string length does not fit in int");
        return -1;
    }
    put = gz_write(state, s, len);
    return len && put == 0 ? -1 : (int)put;
}

/* If the second half of the input buffer is occupied, write out the contents.
   If there is any input remaining due to a non-blocking stall on write, move
   it to the start of the buffer. Return true if this did not open up the
   second half of the buffer.  state->err should be checked after this to
   handle a gz_comp() error. */
static int gz_vacate(gz_state *state) {
    PREFIX3(stream) *strm;

    strm = &(state->strm);
    if ((strm->next_in + strm->avail_in) - state->in <= state->size)
        return 0;
    (void)gz_comp(state, Z_NO_FLUSH);
    if (strm->avail_in == 0) {
        strm->next_in = state->in;
        return 0;
    }
    memmove(state->in, strm->next_in, strm->avail_in);
    strm->next_in = state->in;
    return strm->avail_in > state->size;
}

/* -- see zlib.h -- */
z_int32_t Z_EXPORTVA PREFIX(gzvprintf)(gzFile file, const char *format, va_list va) {
    int len, ret;
    char *next;
    gz_state *state;
    PREFIX3(stream) *strm;

    /* get internal structure */
    if (file == NULL)
        return Z_STREAM_ERROR;
    /* reject a NULL format before it is passed to vsnprintf() */
    if (format == NULL)
        return Z_STREAM_ERROR;
    state = (gz_state *)file;
    strm = &(state->strm);

    /* check that we're writing and that there's no (serious) error */
    if (state->mode != GZ_WRITE || (state->err != Z_OK && !state->again))
        return Z_STREAM_ERROR;
    PREFIX(gz_error)(state, Z_OK, NULL);

    /* make sure we have some buffer space */
    if (state->size == 0 && gz_write_init(state) == -1)
        return state->err;

    /* check for seek request */
    if (state->skip && gz_zero(state) == -1)
        return state->err;

    /* do the printf() into the input buffer, put length in len -- the input
       buffer is double-sized just for this function, so there should be
       state->size bytes available after the current contents */
    ret = gz_vacate(state);
    if (state->err) {
        if (ret && state->again) {
            /* There was a non-blocking stall on write, resulting in the part
               of the second half of the output buffer being occupied.  Return
               a Z_BUF_ERROR to let the application know that this gzprintf()
               needs to be retried. */
            PREFIX(gz_error)(state, Z_BUF_ERROR, "stalled write on gzprintf");
        }
        if (!state->again)
            return state->err;
    }
    if (strm->avail_in == 0)
        strm->next_in = state->in;
    next = (char *)(state->in + (strm->next_in - state->in) + strm->avail_in);
    next[state->size - 1] = 0;
    len = vsnprintf(next, state->size, format, va);

    /* check that printf() results fit in buffer */
    if (len == 0 || (unsigned)len >= state->size || next[state->size - 1] != 0)
        return 0;

    /* update buffer and position */
    strm->avail_in += (unsigned)len;
    state->x.pos += len;

    /* write out buffer if more than half is occupied */
    ret = gz_vacate(state);
    if (state->err && !state->again)
        return state->err;
    return len;
}

z_int32_t Z_EXPORTVA PREFIX(gzprintf)(gzFile file, const char *format, ...) {
    va_list va;
    int ret;

    va_start(va, format);
    ret = PREFIX(gzvprintf)(file, format, va);
    va_end(va);
    return ret;
}

/* -- see zlib.h -- */
z_int32_t Z_EXPORT PREFIX(gzflush)(gzFile file, z_int32_t flush) {
    gz_state *state;

    /* get internal structure */
    if (file == NULL)
        return Z_STREAM_ERROR;
    state = (gz_state *)file;

    /* check that we're writing and that there's no (serious) error */
    if (state->mode != GZ_WRITE || (state->err != Z_OK && !state->again))
        return Z_STREAM_ERROR;
    PREFIX(gz_error)(state, Z_OK, NULL);

    /* check flush parameter */
    if (flush < 0 || flush > Z_FINISH)
        return Z_STREAM_ERROR;

    /* check for seek request */
    if (state->skip && gz_zero(state) == -1)
        return state->err;

    /* compress remaining data with requested flush */
    (void)gz_comp(state, flush);
    return state->err;
}

/* -- see zlib.h -- */
z_int32_t Z_EXPORT PREFIX(gzsetparams)(gzFile file, z_int32_t level, z_int32_t strategy) {
    gz_state *state;
    PREFIX3(stream) *strm;
    z_int32_t ret;

    /* get internal structure */
    if (file == NULL)
        return Z_STREAM_ERROR;
    state = (gz_state *)file;
    strm = &(state->strm);

    /* check that we're compressing and that there's no (serious) error */
    if (state->mode != GZ_WRITE || (state->err != Z_OK && !state->again) ||
            state->direct)
        return Z_STREAM_ERROR;
    PREFIX(gz_error)(state, Z_OK, NULL);

    /* check compression parameters */
    if ((level < Z_NO_COMPRESSION && level != Z_DEFAULT_COMPRESSION) || level > Z_BEST_COMPRESSION ||
        strategy < Z_DEFAULT_STRATEGY || strategy > Z_FIXED)
        return Z_STREAM_ERROR;

    /* if no change is requested, then do nothing */
    if (level == state->level && strategy == state->strategy)
        return Z_OK;

    /* check for seek request */
    if (state->skip && gz_zero(state) == -1)
        return state->err;

    /* change compression parameters for subsequent input */
    if (state->size) {
        /* flush previous input with previous parameters before changing */
        if (strm->avail_in && gz_comp(state, Z_BLOCK) == -1)
            return state->err;
        do {
            ret = PREFIX(deflateParams)(strm, level, strategy);
            /* A hook may require a stronger flush that needs more output
               space. Retry only if the last call filled the output buffer,
               which guarantees progress. */
            if (ret != Z_BUF_ERROR || strm->avail_out != 0)
                break;
            if (gz_comp(state, Z_BLOCK) == -1)
                return state->err;
        } while (ret == Z_BUF_ERROR);
        if (ret != Z_OK)
            return ret;
    }
    state->level = level;
    state->strategy = strategy;
    return Z_OK;
}

/* -- see zlib.h -- */
z_int32_t Z_EXPORT PREFIX(gzclose_w)(gzFile file) {
    int ret = Z_OK;
    gz_state *state;

    /* get internal structure */
    if (file == NULL)
        return Z_STREAM_ERROR;
    state = (gz_state *)file;

    /* check that we're writing */
    if (state->mode != GZ_WRITE)
        return Z_STREAM_ERROR;

    /* check for seek request */
    if (state->skip && gz_zero(state) == -1)
        ret = state->err;

    /* flush, free memory, and close file */
    if (gz_comp(state, Z_FINISH) == -1)
        ret = state->err;
    if (state->size) {
        if (!state->direct) {
            (void)PREFIX(deflateEnd)(&(state->strm));
        }
        gz_buffer_free(state);
    }
    PREFIX(gz_error)(state, Z_OK, NULL);
    free(state->path);
    if (close(state->fd) == -1)
        ret = Z_ERRNO;
    gz_state_free(state);
    return ret;
}
