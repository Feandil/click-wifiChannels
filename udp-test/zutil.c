#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include "debug.h"
#include "zutil.h"

void
add_data(struct zutil* in, char* data, size_t len)
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
end_data(struct zutil* in)
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
        exit(1);
      }
      in->strm.avail_out = OUT_BUF_SIZE;
      in->strm.next_out = (Bytef*)in->out;
    }
    ret = deflate(&in->strm, Z_FINISH);
    assert(ret != Z_STREAM_ERROR);
  } while (in->strm.avail_out != OUT_BUF_SIZE);
  (void)deflateEnd(&in->strm);
}

int
zinit(struct zutil* buffer, FILE *out,const int encode)
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
