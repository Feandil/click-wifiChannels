#define _GNU_SOURCE

#include <assert.h>
#include <inttypes.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <zlib.h>

#ifdef DEBUG
  #define PERROR(x) perror(x);
  #define PRINTF(...) printf(__VA_ARGS__);
#else
  #define PERROR(x)
  #define PRINTF(...)
#endif

/* Global cache */
#define BUF_DIVISOR    4
#define BUF_SIZE    1024
#define ERROR_SIZE   128
z_stream strm;
FILE *input;
FILE *output;
uInt trash;
uint32_t count;
char in[BUF_SIZE];
char buf1[BUF_SIZE];
char buf2[BUF_SIZE];
char err[ERROR_SIZE];

#define VERIFY_FWRITE(len, file)              \
  if ((ret != (int)(len)) || ferror(file)) {  \
    PRINTF("Unable to write to outputfile\n") \
    exit(1);                                  \
  }

#define MIN_LEN  6      // " IP address: 3 '.', ',' '.' ','

inline static void extract(const char* incoming, const uInt len) {
  int ret;
  const char* count_pos;
  uint32_t max;
  if (len < MIN_LEN) {
    return;
  }
  count_pos = memrchr(incoming, ',', len);
  if (sscanf(count_pos + 1, "%"SCNu32, &max) != 1) {
    PRINTF("Bad input\n")
    exit(1);
  }
  if (count == 0) {
    count = max;
    return;
  }
  while (max - count - 1 > 0) {
    if (max - count - 1> ERROR_SIZE) {
      ret = fwrite(err, BUF_DIVISOR, ERROR_SIZE / BUF_DIVISOR, output);
      VERIFY_FWRITE(ERROR_SIZE / BUF_DIVISOR, output);
      count += ERROR_SIZE;
    } else {
      ret = fwrite(err, 1, max - count - 1, output);
      VERIFY_FWRITE(max - count - 1, output);
      break;
    }
  }
  ret = fwrite("1", 1, 1, output);
  VERIFY_FWRITE(1, output);
  count = max;
}

inline static void consume(const bool swaped) {
  char* newIn;
  char* trashed;
  char* pos;
  uInt available;
  uInt temp_len;

  if (swaped) {
    newIn = buf2;
    trashed = buf1;
  } else {
    newIn = buf1;
    trashed = buf2;
  }
  assert(BUF_SIZE >= strm.avail_out);
  available = BUF_SIZE - strm.avail_out;
  pos = memchr(newIn, '\n', available);
  while(pos != NULL) {
    temp_len = pos - newIn + 1;
    available -= temp_len;
    *pos = '\0';
    if (trash != 0) {
      memcpy(trashed + trash, newIn, temp_len);
      extract(trashed, trash + temp_len);
      trash = 0;
    } else {
      extract(newIn, temp_len);
    }
    if (available == 0) {
      trash = 0;
      return;
    } else {
      newIn = pos + 1;
    }
    pos = memchr(newIn, '\n', available);
  }
  trash = available;
  if (swaped) {
    memcpy(buf2, newIn, trash);
  } else {
    memcpy(buf1, newIn, trash);
  }
}

static int load(const bool swaped) {
  int ret;
  int flush;

  assert(feof(input) == 0);
  assert(strm.avail_out == 0);
  assert(swaped ? (strm.next_out == (Bytef*) buf2) : (strm.next_out == (Bytef*) buf1));

  strm.avail_out = BUF_SIZE;
  flush = Z_NO_FLUSH;
  while (strm.avail_out != 0 && flush == Z_NO_FLUSH) {
    if (strm.avail_in == 0) {
      strm.next_in = (Bytef*) in;
      strm.avail_in = fread(in, 1, BUF_SIZE, input);
      if (ferror(input)) {
        return -1;
      }
    }
    flush = (feof(input)) ? Z_FINISH : Z_NO_FLUSH;
    ret = inflate(&strm, flush);
    assert(ret != Z_STREAM_ERROR);
  }
  strm.next_out = (Bytef*) (swaped ? buf1 : buf2);
  return 0;
}

inline static void load_final(bool swaped) {
  int ret;

  assert(feof(input) != 0);
  assert(swaped ? (strm.next_out == (Bytef*)buf2) : (strm.next_out == (Bytef*)buf1));

  do {
    strm.avail_out = BUF_SIZE;
    ret = inflate(&strm, Z_FINISH);
    assert(ret != Z_STREAM_ERROR);
    consume(swaped);
    swaped = !swaped;
    strm.next_out = (Bytef*) (swaped ? buf2 : buf1);
  } while (strm.avail_out != BUF_SIZE);
}

static void usage(int error)
{
  printf("listen: Listen on a given socket and print packets content\n");
  printf("Usage: ./listen [OPTIONS]\n");
  printf("Options:\n");
  printf(" -h, --help           Print this ...\n");
  printf(" -i, --ouput  <file>  Specify the output file (default : standard input)\n");
  printf(" -o, --ouput  <file>  Specify the output file (default : standard output)\n");

  exit(error);
}

static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"input",       required_argument, 0,  'l' },
  {"output",      required_argument, 0,  'o' },
  {NULL,                          0, 0,   0  }
};

int main(int argc, char *argv[]) {
  int opt, ret;
  bool swap;
  char *in_filename = NULL;
  char *out_filename = NULL;

  while((opt = getopt_long(argc, argv, "hi:o:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage(0);
        return 0;
      case 'i':
        in_filename = optarg;
        break;
      case 'o':
        out_filename = optarg;
        break;
      default:
        usage(1);
        break;
    }
  }

 if(argc > optind) {
    usage(1);
    return 1;
  }

  if (in_filename != NULL) {
    input = fopen(in_filename, "r");
    if (input == NULL) {
      printf("Unable to open intput file\n");
      return -1;
    }
  } else {
    input = stdin;
  }

  if (out_filename != NULL) {
    output = fopen(out_filename, "w");
    if (input == NULL) {
      printf("Unable to open output file\n");
      return -1;
    }
  } else {
    output = stdout;
  }

  /* Initialisation */
  /* Initialize zlib */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  ret = inflateInit2(&strm, 15 + 16);
  if (ret != Z_OK) {
    PRINTF("ZLIB initialization error : %i\n", ret)
    return -2;
  }

  /* Initialize buffers */
  swap = false;
  trash = 0;
  count = 0;
  memset(err, '0', ERROR_SIZE);
  strm.next_out = (Bytef*) buf1;

  while (!feof(input)){
    if (load(swap) < 0) {
      printf("Error\n");
      return -3;
    }
    consume(swap);
    swap = !swap;
  }
  load_final(swap);
  return 0;
}
