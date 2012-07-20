#ifndef ZUTIL_H
#define ZUTIL_H

#include <zlib.h>
#include <stdio.h>
#include <stdbool.h>


#define OUT_BUF_SIZE  1500
#define IN_BUF_SIZE   4*4096
#define INC_BUF_SIZE  4096

struct zutil_write {
  char out[OUT_BUF_SIZE];
  z_stream strm;
  FILE *output;
};

struct zutil_read {
  char in[(2 * IN_BUF_SIZE) + 1];
  char inc[INC_BUF_SIZE];
  z_stream strm;
  char *start;
  char *end;
  bool swapped;
  FILE *input;
};

int zinit_write(struct zutil_write* buffer, FILE *out, const int encode);
void zadd_data(struct zutil_write *in, const char *data, const size_t len);
void zend_data(struct zutil_write *in);

int zinit_read(struct zutil_read* buffer, FILE *in);
char* zread_line(struct zutil_read *buffer, ssize_t *len);
void zread_end(struct zutil_read *buffer);

#endif /* ZUTIL_H */
