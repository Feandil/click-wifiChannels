#ifndef ZUTIL_H
#define ZUTIL_H

#include <zlib.h>
#include <stdio.h>


#define OUT_BUF_SIZE  1500

struct zutil_write {
  char out[OUT_BUF_SIZE];
  z_stream strm;
  FILE *output;
};


int zinit_write(struct zutil_write* buffer, FILE *out, const int encode);
void zadd_data(struct zutil_write *in, const char *data, const size_t len);
void zend_data(struct zutil_write *in);

#endif /* ZUTIL_H */
