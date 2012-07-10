#ifndef ZUTIL_H
#define ZUTIL_H

#include <zlib.h>
#include <stdio.h>


#define OUT_BUF_SIZE  1500

struct zutil {
  char out[OUT_BUF_SIZE];
  z_stream strm;
  FILE *output;
};


int zinit(struct zutil* buffer, FILE *out, const int encode);
void add_data(struct zutil *in, const char *data, const size_t len);
void end_data(struct zutil *in);

#endif /* ZUTIL_H */
