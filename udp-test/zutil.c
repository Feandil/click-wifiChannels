#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "debug.h"
#include "zutil.h"

void
zadd_data(struct zutil_write* in, const char* data, size_t len)
{
  int ret;
  assert(in->strm.avail_in == 0);
  assert(in->strm.avail_out != 0);
  in->strm.next_in = (Bytef *)data;
  in->strm.avail_in = len;
  while (in->strm.avail_in != 0) {
    ret = deflate(&in->strm, Z_NO_FLUSH);
    assert(ret != Z_STREAM_ERROR);
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

void
zend_data(struct zutil_write* in)
{
  size_t available;
  int ret;

  assert(in != NULL);
  assert(in->strm.avail_in == 0);
  assert(in->strm.avail_out != 0);
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
  (void)deflateEnd(&in->strm);
  fclose(in->output);
}

int
zinit_write(struct zutil_write* buffer, FILE *out, const int encode)
{
  int ret;

  /* The structure is supposed to be zeroed */
  buffer->output = out;
  buffer->strm.zalloc = Z_NULL;
  buffer->strm.zfree = Z_NULL;
  buffer->strm.opaque = Z_NULL;
  ret = deflateInit2(&buffer->strm, encode, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  if (ret != Z_OK) {
    PRINTF("ZLIB initialization error : %i\n", ret)
    return -1;
  }
  buffer->strm.next_out = (Bytef *)buffer->out;
  buffer->strm.avail_out = OUT_BUF_SIZE;

  return 0;
}

static int
zutil_read_input(struct zutil_read *buffer)
{
  int ret;

  if (feof(buffer->input) != 0 && buffer->strm.avail_in == 0) {
    if (buffer->input) {
      fclose(buffer->input);
      buffer->input = NULL;
    }
    return -1;
  }

  buffer->swapped = !buffer->swapped;
  if (buffer->swapped) {
    buffer->strm.next_out = (Bytef*) (buffer->in + IN_BUF_SIZE);
  } else {
    buffer->strm.next_out = (Bytef*) buffer->in;
  }
  buffer->strm.avail_out = IN_BUF_SIZE;

  do {
    if (buffer->strm.avail_in == 0) {
      if (feof(buffer->input) != 0) {
        return 0;
      }
      buffer->strm.next_in = (Bytef*) buffer->inc;
      buffer->strm.avail_in = fread(buffer->inc, 1, INC_BUF_SIZE, buffer->input);
      if (ferror(buffer->input)) {
        return -2;
      }
    }
    ret = inflate(&buffer->strm, Z_NO_FLUSH);
    if (ret <= Z_OK) {
      return (ret - Z_OK);
    }
  } while (buffer->strm.avail_out != 0);
  return 0;
}

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

char*
zread_line(struct zutil_read *buffer, ssize_t *len)
{
  int ret;
  char *next,
       *cur;

  if (buffer->start >= buffer->end) {
    next = NULL;
  } else {
    next = memchr(buffer->start, '\n', (size_t)(buffer->end - buffer->start)); // We just checked it was >= 0
  }

  if (next != NULL) {
    cur = buffer->start;
    *next = '\0';
    *len = next - cur;
    buffer->start = next + 1;
    return cur;
  } else if (buffer->input != NULL) {
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
    if (buffer->swapped) {
      if (buffer->end != buffer->in + IN_BUF_SIZE) {
        assert(buffer->end >= buffer->start);
        cur = buffer->in + IN_BUF_SIZE - (buffer->end - buffer->start);
        memmove(cur, buffer->start, (size_t)(buffer->end - buffer->start));
      } else {
        cur = buffer->start;
      }
      buffer->end = buffer->in + (2 * IN_BUF_SIZE) - buffer->strm.avail_out;
      assert(buffer->end >= cur);
      next = memchr(cur, '\n', (size_t)(buffer->end - cur));
      if (next != NULL) {
        *next = '\0';
        *len = next - cur;
        buffer->start = next + 1;
        return cur;
      } else if (buffer->strm.avail_out != 0) {
        if (cur == buffer->end) {
          *len = - 1;
          return NULL;
        }
        *buffer->end = '\0'; // buffer->strm.avail_out != 0 thus buffer->end < buffer->in + (2 * IN_BUF_SIZE)
        *len = buffer->end - buffer->start;
        buffer->start = buffer->end;
        return cur;
      } else {
        *len = - 2;
        PRINTF("zutil read : line too long 1\n")
        return NULL;
      }
    } else {
      next = memchr(buffer->in, '\n', IN_BUF_SIZE - buffer->strm.avail_out);
      if (next != NULL) {
        assert(next >= buffer->in);
        *next = '\0';
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
