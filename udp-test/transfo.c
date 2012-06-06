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
  #define OPRINTF(...) fprintf(output, __VA_ARGS__);
#else
  #define PERROR(x)
  #define PRINTF(...)
  #define OPRINTF(...)
#endif

/* Global cache */
#define BUF_DIVISOR    4
#define BUF_SIZE    1024
z_stream strm;
FILE *input;
FILE *output;
uInt trash;
uint32_t count;
char in[BUF_SIZE];
char buf1[BUF_SIZE];
char buf2[BUF_SIZE];

char *zeroes;
size_t tries;
int32_t *sec_limits, *nsec_limits;
size_t global_buffer_size, global_buffer_pos;
int32_t *global_buffer_sec, *global_buffer_nsec;
uint32_t *global_buffer_num;
int32_t sec_min, nsec_min;

#define FWRITE(file, c, bloc, len)            \
  ret = fwrite(c, bloc, len, file);           \
  if ((ret != (int)(len))) {                  \
    PRINTF("Unable to write to outputfile: ") \
    PRINTF("%i VS %u (%u)\n", ret, len, bloc) \
    exit(1);                                  \
  } else if (ferror(file)) {                  \
    PERROR("fwrite");                         \
    exit(1);                                  \
  }

#define MEMCHR(dest, src, tgt, len)           \
  dest = memchr(src, tgt, len);               \
  if (dest == NULL) {                         \
    PRINTF("Bad format (%.*s)\n", len, src);  \
    exit(1);                                  \
  }                                           \
  *dest = 0;                                  \
  ++dest;                                     \
  len -= dest - src;

#define SSCANF(dest, pos)                     \
  if (sscanf(pos, "%"SCNd32, &dest) != 1) {   \
    PRINTF("Bad format (NaN : %s)\n", pos)    \
    exit(2);                                  \
  }

#define MIN_LEN  6      // " IP address: 3 '.', ',' '.' ','

inline static void extract(char* incoming, uInt len) {
  int ret;
  char *sec1_pos, *nsec1_pos, *sec2_pos, *nsec2_pos, *count_pos;
  uint32_t max, tmp;
  int32_t sec1, nsec1, sec2, nsec2, sec, nsec;
  size_t i, j;

  if (len < MIN_LEN) {
    return;
  }
  MEMCHR(sec1_pos,   incoming, ',', len)
  MEMCHR(nsec1_pos,  sec1_pos, '.', len)
  MEMCHR(sec2_pos,  nsec1_pos, ',', len)
  MEMCHR(nsec2_pos,  sec2_pos, '.', len)
  MEMCHR(count_pos, nsec2_pos, ',', len)

  SSCANF(sec1,   sec1_pos);
  SSCANF(nsec1, nsec1_pos);
  SSCANF(sec2,   sec2_pos);
  SSCANF(nsec2, nsec2_pos);
  SSCANF(max,   count_pos);

  assert(count <= max);
  if (count == 0) {
    count = max;
    return;
  }
  if (nsec1 < nsec2) {
    nsec = 1000000000 - nsec2 + nsec1;
    sec = sec1 - sec2 - 1;
  } else {
    nsec = nsec1 - nsec2;
    sec = sec1 - sec2;
  }

  if (global_buffer_pos < global_buffer_size) {
    if ((sec < sec_min) || ((sec = sec_min) && (nsec < nsec_min))) {
      sec_min = sec;
      nsec_min = nsec;
    }
  } else if (sec_min) {
    for (i = 0; i < global_buffer_size; ++i) {
      assert(global_buffer_sec[i] >= sec_min);
      assert(global_buffer_nsec[i] >= nsec_min);
      if (tries != INT32_MAX) {
        for (tmp = count; tmp < global_buffer_num[i] - 1; ++ tmp) {
          FWRITE(output, zeroes, tries, 1);
        }
        for (j = 0; j < tries; ++j) {
          if (((global_buffer_sec[i] - sec_min) < sec_limits[j]) || (((global_buffer_sec[i] - sec_min) == sec_limits[j]) && ((global_buffer_nsec[i] - nsec_min) < nsec_limits[j]))) {
            break;
          }
          FWRITE(output, "0", 1, 1);
        }
        FWRITE(output, "1", 1, 1);
        count = global_buffer_num[i];
      } else {
        OPRINTF("%"PRIi32": ", global_buffer_num[i])
        fprintf(output, "%"PRIi32".%09"PRIi32"\n", (global_buffer_sec[i] - sec_min), (global_buffer_nsec[i] - nsec_min));
      }
    }
    sec_min = sec;
    nsec_min = nsec;
    global_buffer_pos = 0;
  } else {
    sec_min = sec;
    nsec_min = nsec;
    global_buffer_pos = 0;
    count = max;
  }
  global_buffer_sec[global_buffer_pos] = sec;
  global_buffer_nsec[global_buffer_pos] = nsec;
  global_buffer_num[global_buffer_pos] = max;
  ++global_buffer_pos;
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

#define DEFAULT_CACHE_SIZE 2000

static void usage(int error, char *name)
{
  printf("%s: Try to transform the input into a 0 and 1\n", name);
  printf("Usage: %s [OPTIONS]\n", name);
  printf("Options:\n");
  printf(" -h, --help           Print this ...\n");
  printf(" -i, --ouput  <file>  Specify the output file (default: standard input)\n");
  printf(" -o, --ouput  <file>  Specify the output file (default: standard output)\n");
  printf(" -l, --limit  <lims>  Coma-separated sec.(%%09)nsec values of the retries timing.\n");
  printf(" -c, --cache  <size>  Size of the cache for the clock synchronization (Default value: %u)\n", DEFAULT_CACHE_SIZE);

  exit(error);
}

static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"input",       required_argument, 0,  'i' },
  {"output",      required_argument, 0,  'o' },
  {"limit",       required_argument, 0,  'l' },
  {"cache",       required_argument, 0,  'c' },
  {NULL,                          0, 0,   0  }
};

int main(int argc, char *argv[]) {
  int opt, ret;
  bool swap;
  char *in_filename = NULL;
  char *out_filename = NULL;
  const char *position;
  char *mutable_optarg;
  size_t i;
  global_buffer_size = DEFAULT_CACHE_SIZE;
  tries = INT32_MAX;

  while((opt = getopt_long(argc, argv, "hi:o:l:c:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage(0, argv[0]);
        return 0;
      case 'i':
        in_filename = optarg;
        break;
      case 'o':
        out_filename = optarg;
        break;
      case 'l':
        if (tries != INT32_MAX) {
         usage(1, argv[0]);
        }
        tries = 0;
        position = optarg;
        do {
          position = strchr(position, ',');
          ++tries;
        } while (position != NULL);
        mutable_optarg = strdup(optarg);
        sec_limits = malloc(tries * sizeof(int32_t));
        nsec_limits = malloc(tries * sizeof(int32_t));
        zeroes = malloc(tries);
        if ((sec_limits == NULL) || (nsec_limits == NULL) || (zeroes == NULL)) {
          PRINTF("Unable to malloc\n")
          PERROR("malloc")
          exit(-2);
        }
        mutable_optarg = strtok(mutable_optarg, ".");
        for (i = 0; i < tries; ++i) {
          SSCANF(sec_limits[i],  mutable_optarg)
          mutable_optarg = strtok(NULL, ",");
          SSCANF(nsec_limits[i],  mutable_optarg)
          mutable_optarg = strtok(NULL, ".");
        }
        memset(zeroes, '0', tries);
        break;
      case 'c':
        if (global_buffer_size != DEFAULT_CACHE_SIZE) {
          usage(1, argv[0]);
        }
        SSCANF(global_buffer_size, optarg)
        break;
      default:
        usage(-1, argv[0]);
        break;
    }
  }

 if(argc > optind) {
    usage(1, argv[0]);
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
  strm.next_out = (Bytef*) buf1;

  global_buffer_sec = malloc(global_buffer_size * sizeof(int32_t));
  global_buffer_nsec = malloc(global_buffer_size * sizeof(int32_t));
  global_buffer_num = malloc(global_buffer_size * sizeof(int32_t));

  if ((global_buffer_num == NULL) || (global_buffer_nsec == NULL) || (global_buffer_sec == NULL)) {
    PRINTF("malloc error\n");
    return -5;
  }

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
