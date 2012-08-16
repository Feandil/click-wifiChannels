#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "debug.h"
#include "zutil.h"

/** @file zutil.c Implementation of the zlib interface.
 * This file contain direct interfaces to the zlib API.
 * Most of this function can be deduced more or less directly from the zlib documentation
 */

/**
 * Add data to a encoding stream (call exit(1) in case of failure).
 * @param in   Opaque structure describing the stream, need to be initialized by zinit_write.
 * @param data Pointer to the memory zone to be added to the stream.
 * @param len  Length of memory zone to be added to the stream.
 */
void
zadd_data(struct zutil_write* in, const char* data, size_t len)
{
  int ret;

  assert(in->strm.avail_in == 0);
  assert(in->strm.avail_out != 0);

  /* Give the memory zone to zlib */
  in->strm.next_in = (Bytef *)data;
  in->strm.avail_in = len;

  /* Keep invoking zlib until all the input disapeared */
  while (in->strm.avail_in != 0) {
    ret = deflate(&in->strm, Z_NO_FLUSH);
    assert(ret != Z_STREAM_ERROR);

    /* If the buffer is full, flush it to the file */
    if (in->strm.avail_out == 0) {
      if (fwrite(in->out, 1, OUT_BUF_SIZE, in->output) != OUT_BUF_SIZE || ferror(in->output)) {
        PRINTF("Unable to write to outputfile\n")
        (void)deflateEnd(&in->strm);
        exit(1);
      }
      in->strm.avail_out = OUT_BUF_SIZE;
      in->strm.next_out = (Bytef *)in->out;
    }
  }
}

/**
 * Close a opened encoding stream (Close the file used for the output).
 * @param in Opaque structure describing the stream, need to be initialized by zinit_write and not closed.
 */
void
zend_data(struct zutil_write* in)
{
  size_t available;
  int ret;

  assert(in != NULL);
  assert(in->strm.avail_in == 0);
  assert(in->strm.avail_out != 0);

  /* We need to flush all the data contained in the zlib buffer */
  do {
    available = OUT_BUF_SIZE - in->strm.avail_out;
    if (available != 0) {
      if (fwrite(in->out, 1, available, in->output) != available || ferror(in->output)) {
        PRINTF("Unable to write to outputfile\n")
        (void)deflateEnd(&in->strm);
        fclose(in->output);
        exit(1);
      }
      in->strm.avail_out = OUT_BUF_SIZE;
      in->strm.next_out = (Bytef*)in->out;
    }
    ret = deflate(&in->strm, Z_FINISH);
    assert(ret != Z_STREAM_ERROR);
  } while (in->strm.avail_out != OUT_BUF_SIZE);
  /* Clean zlib state */
  (void)deflateEnd(&in->strm);
  /* Close the file */
  fclose(in->output);
}

/**
 * Open a new encoding stream.
 * @param buffer Zeroed memory pointer for the internal opaque structure.
 * @param out    Output file.
 * @param encode Level of the zlib encoding (1-9).
 * @return 0 in case of succes, -1 in case of error.
 */
int
zinit_write(struct zutil_write* buffer, FILE *out, const int encode)
{
  int ret;

  /* File verification */
  if (out == NULL || ferror(out)) {
    PRINTF("ZUTIL ERROR, output file error\n")
    return -1;
  }
  /* The structure is supposed to be zeroed */
  buffer->output = out;
  buffer->strm.zalloc = Z_NULL;
  buffer->strm.zfree = Z_NULL;
  buffer->strm.opaque = Z_NULL;
  /* This init is using deflateInit2 with special values in order to have a gzip-compatible output */
  ret = deflateInit2(&buffer->strm, encode, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  if (ret != Z_OK) {
    PRINTF("ZLIB initialization error : %i\n", ret)
    return -1;
  }
  buffer->strm.next_out = (Bytef *)buffer->out;
  buffer->strm.avail_out = OUT_BUF_SIZE;

  return 0;
}

/**
 * Read data from an decoding stream.
 * Fill half of the in buffer, partially in case of EOF
 * @param buffer Opaque structure describing the stream, need to be initialized by zinit_read.
 * @return 0 in case of success, -1 for EOF, -2 for file error and >=0 for zlib errors.
 */
static int
zutil_read_input(struct zutil_read *buffer)
{
  int ret;

  /* Verify that we can read the file */
  if (feof(buffer->input) != 0 && buffer->strm.avail_in == 0) {
    if (buffer->input) {
      fclose(buffer->input);
      buffer->input = NULL;
    }
    return -1;
  }

  /* "Swap" the buffer */
  buffer->swapped = !buffer->swapped;
  if (buffer->swapped) {
    buffer->strm.next_out = (Bytef*) (buffer->in + IN_BUF_SIZE);
  } else {
    buffer->strm.next_out = (Bytef*) buffer->in;
  }
  buffer->strm.avail_out = IN_BUF_SIZE;

  /* Load from zlib until no place remain in the destination buffer */
  do {
    if (buffer->strm.avail_in == 0) {
      /* Enf of file ? */
      if (feof(buffer->input) != 0) {
        return 0;
      }
      /* Add new input */
      buffer->strm.next_in = (Bytef*) buffer->inc;
      buffer->strm.avail_in = fread(buffer->inc, 1, INC_BUF_SIZE, buffer->input);
      if (ferror(buffer->input)) {
        return -2;
      }
    }
    /* Zlib call */
    ret = inflate(&buffer->strm, Z_NO_FLUSH);
    if (ret <= Z_OK) {
      return (ret - Z_OK);
    }
  } while (buffer->strm.avail_out != 0);
  return 0;
}

/**
 * Open a new decoding stream.
 * @param buffer Zeroed memory pointer for the internal opaque structure.
 * @param in     Input file.
 * @return 0 in case of succes, -1 in case of error.
 */
int
zinit_read(struct zutil_read* buffer, FILE *in)
{
  int ret;

  /* The structure is supposed to be zeroed */
  buffer->strm.zalloc = Z_NULL;
  buffer->strm.zfree = Z_NULL;
  buffer->strm.opaque = Z_NULL;
  ret = inflateInit2(&buffer->strm, 15 + 16);
  if (ret != Z_OK) {
    PRINTF("ZLIB initialization error : %i\n", ret)
    return -1;
  }

  /* Fill the buffer with data from the file to test it (see if it's really a gzipped file) */
  /* swapped is set to true so that the zutil_read_input call sets it to false an uses the first part of the buffer */
  buffer->swapped = true;
  buffer->input = in;
  buffer->strm.avail_in = 0;
  ret = zutil_read_input(buffer);
  if (ret < -1) {
   return ret;
  }
  buffer->start = buffer->in;
  buffer->end = buffer->in + IN_BUF_SIZE - buffer->strm.avail_out;

  return 0;
}

/**
 * Read a new line (ended by '\n' or EOF) from the decoding stream
 * @param buffer The opaque structure describing the decoding stream, need to have been initialized by 'zinit_read'
 * @param len    Pointer to a ssize_t in which the length of the new line will be placed
 * @return A '\0' ended string which length is contain in len, NULL in case of failure (len will contain the error, -1 for EOF)
 */
char*
zread_line(struct zutil_read *buffer, ssize_t *len)
{
  int ret;
  char *next,
       *cur;

  /* Check if we already have a full line in of buffer */
  if (buffer->start < buffer->end) {
    next = memchr(buffer->start, '\n', (size_t)(buffer->end - buffer->start)); // We just checked it was >= 0
    if (next != NULL) {
      cur = buffer->start;
      *next = '\0';
      *len = next - cur;
      buffer->start = next + 1;
      return cur;
    }
  }

  /* We dont have any line thus let's try to load data from zlib into our buffer */
  if (buffer->input != NULL) {
    ret = zutil_read_input(buffer);
    if (ret < 0) {
      if (ret == -1) {
        *len = - 1;
        return NULL;
      }
      *len = - 2;
      PRINTF("zutil read error : %i\n", ret)
      return NULL;
    }
    /* We now need to update the start/end pointers and find the end of our line
       It first depend on what buffer we are using */
    if (buffer->swapped) {
      /* We need to re-attach the remaning part of the other part cache if it's detached */
      if (buffer->end != buffer->in + IN_BUF_SIZE) {
        assert(buffer->end >= buffer->start);
        cur = buffer->in + IN_BUF_SIZE - (buffer->end - buffer->start);
        memmove(cur, buffer->start, (size_t)(buffer->end - buffer->start));
      } else {
        cur = buffer->start;
      }
      /* Update the "end" pointer */
      buffer->end = buffer->in + (2 * IN_BUF_SIZE) - buffer->strm.avail_out;
      /* Search the '\n' */
      assert(buffer->end >= cur);
      next = memchr(cur, '\n', (size_t)(buffer->end - cur));
      if (next != NULL) {
        /* Found, return */
        *next = '\0';
        *len = next - cur;
        buffer->start = next + 1;
        return cur;
      } else if (buffer->strm.avail_out != 0) {
        /* Not found, but we didn't have a full cache thus we reached the end of the file */
        if (cur == buffer->end) {
          /* Nothing left, return EOF */
          *len = - 1;
          return NULL;
        }
        /* Return what is left */
        *buffer->end = '\0'; // buffer->strm.avail_out != 0 thus buffer->end < buffer->in + (2 * IN_BUF_SIZE)
        *len = buffer->end - buffer->start;
        buffer->start = buffer->end;
        return cur;
      } else {
        /* The line is longer than our cache, this algorithm doesn't support it, return */
        *len = - 2;
        PRINTF("zutil read : line too long 1\n")
        return NULL;
      }
    } else {
      /* Search the '\n' */
      next = memchr(buffer->in, '\n', IN_BUF_SIZE - buffer->strm.avail_out);
      if (next != NULL) {
        assert(next >= buffer->in);
        *next = '\0';
        /* If we had remaining parts in the other buffer, we need to put those two together */
        if (buffer->start < buffer->end) {
          *len = (buffer->end - buffer->start) + (next - buffer->in);
          if (*len >= IN_BUF_SIZE) {
            *len = -2;
            PRINTF("zutil read : line too long 2\n")
            return NULL;
          }
          memmove(buffer->in + IN_BUF_SIZE, buffer->start, (size_t) (buffer->end - buffer->start)); // Checked in the "if"
          memmove(buffer->in + IN_BUF_SIZE + (buffer->end - buffer->start), buffer->in, (size_t) (next - buffer->in));
          *(buffer->in + IN_BUF_SIZE + *len) = '\0';
          buffer->start = next + 1;
          buffer->end = buffer->in + IN_BUF_SIZE - buffer->strm.avail_out;
          return buffer->in + IN_BUF_SIZE;
        } else {
          buffer->start = next + 1;
          buffer->end = buffer->in + IN_BUF_SIZE - buffer->strm.avail_out;
          *len = next - buffer->in;
          return buffer->in;
        }
      } else if (buffer->strm.avail_out != 0) {
        /* Not found, but we didn't have a full cache thus we reached the end of the file */
        assert(buffer->strm.avail_out <= IN_BUF_SIZE);
        if (buffer->strm.avail_out == IN_BUF_SIZE) {
          if (buffer->start != buffer->end) {
            *buffer->end = '\0';
            *len = buffer->end - buffer->start;
            cur = buffer->start;
            buffer->start = buffer->end;
            return cur;
          } else {
            *len = - 1;
            return NULL;
          }
        } else if (buffer->start == buffer->end) {
          *len = (ssize_t)(IN_BUF_SIZE - buffer->strm.avail_out); // We just checked that this was > 0 and it's < IN_BUF_SIZE which should be < SSIZE_MAX
          return buffer->in;
        } else {
          *len = (buffer->end - buffer->start) + ((ssize_t)(IN_BUF_SIZE - buffer->strm.avail_out)); // We just checked that this was > 0 and it's < IN_BUF_SIZE which should be < SSIZE_MAX
          if (*len >= IN_BUF_SIZE) {
            *len = -2;
            PRINTF("zutil read : line too long 3\n")
            return NULL;
          }
          assert(buffer->end >= buffer->start);
          memmove(buffer->in + IN_BUF_SIZE, buffer->start, (size_t)(buffer->end - buffer->start));
          memmove(buffer->in + IN_BUF_SIZE + (buffer->end - buffer->start), buffer->in, IN_BUF_SIZE - buffer->strm.avail_out);
          *(buffer->in + IN_BUF_SIZE + *len) = '\0';
          buffer->start = buffer->in + IN_BUF_SIZE;
          buffer->end = buffer->in + IN_BUF_SIZE;
          return buffer->in + IN_BUF_SIZE;
        }
     } else {
        *len = - 2;
        PRINTF("zutil read : line too long 4\n")
        return NULL;
      }
    }
  }

  return NULL;
}

/**
 * Close a decoding stream, close the corresponding file
 * @param buffer The opaque structure describing the decoding stream, need to have been initialized by 'zinit_read'
 */
void
zread_end(struct zutil_read *buffer)
{
  (void)inflateEnd(&buffer->strm);
  if (buffer->input != NULL) {
    fclose(buffer->input);
    buffer->input = NULL;
  }
}

#ifdef TEST
/* A small zcat emulation using this library, for testing purposes */
int
main(int argc, char *argv[])
{
  FILE *in;
  struct zutil_read buffer;
  ssize_t len;
  const char* output;

  while (argc > 1) {
    ++argv;
    --argc;
    memset(&buffer, 0, sizeof(struct zutil_read));
    in = fopen(*argv, "r");
    if (in == NULL) {
      printf("Unable to load %s\n", *argv);
      return -1;
    }
    zinit_read(&buffer, in);
    while ((output = zread_line(&buffer, &len)) != NULL) {
      printf("%.*s\n", len, output);
    }
  }
  return 0;
}
#endif
