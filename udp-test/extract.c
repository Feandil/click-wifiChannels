#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <getopt.h>
#include <gsl/gsl_cdf.h>
#include <math.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "debug.h"
#include "zutil.h"

#define M_PIl          3.141592653589793238462643383279502884L

/* File reading struct */
struct extract_io {
  struct zutil_read input;
  struct in6_addr     src;
  bool       fixed_ip;
  bool         origin;
  uint64_t      count;
  uint64_t last_count;
  double     timestamp;
};

/* Global cache */
#define MAX_SOURCES 2
struct extract_io in[MAX_SOURCES];
uint64_t sync_count_diff;
double   secure_interval;
uint64_t u64_stats[2][2];

/* Output */
FILE* output;

static ssize_t
read_input(struct extract_io *inc)
{
  ssize_t len;
  char *buffer, *next;
  struct in6_addr ip;
  int tmp;

  buffer = zread_line(&inc->input, &len);
  if (buffer == NULL) {
    if (len < -1) {
      goto exit;
    }
    return -1;
  }
  assert(len > 0);

  /* Verify the IP address */
  next = memchr(buffer, ',', len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the IP address field : ''%.*s'')\n", len, buffer);
    goto exit;
  }
  *next = '\0';
  tmp = inet_pton(AF_INET6, buffer, &ip);
  assert(tmp != -1);
  if (tmp == 0) {
    printf("Invalid IPv6 address '%s'\n", buffer);
    goto exit;
  }

  if (memcmp(&ip, &inc->src, sizeof(struct in6_addr)) != 0) {
    if (inc->fixed_ip) {
      return 0;
    }
    if (*inc->src.s6_addr32 == 0) {
      memcpy(&inc->src, &ip, sizeof(struct in6_addr));
    } else {
      printf("No from address was specified but two different addresses appeared\n");
      goto exit;
    }
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Skip the flag field */
  next = memchr(buffer, ',', len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the flag field : ''%.*s'')\n", len, buffer);
    goto exit;
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Skip the signal field */
  next = memchr(buffer, ',', len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the signal field : ''%.*s'')\n", len, buffer);
    goto exit;
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Skip the signal field */
  next = memchr(buffer, ',', len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the rate field : ''%.*s'')\n", len, buffer);
    goto exit;
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Look at the origin timestamp field */
  next = memchr(buffer, ',', len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the origin timestamp field : ''%.*s'')\n", len, buffer);
    goto exit;
  }
  if (inc->origin) {
    *next = '\0';
    tmp = sscanf(buffer, "%lf", &inc->timestamp);
    if (tmp != 1) {
      printf("Bad input format (origin timestamp isn't a double: ''%.*s'')\n", len, buffer);
      goto exit;
    }
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Read the count field */
  next = memchr(buffer, ',', len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the sent timestamp field : ''%.*s'')\n", len, buffer);
    goto exit;
  }
  *next = '\0';
  inc->last_count = inc->count;
  tmp = sscanf(buffer, "%"SCNd64, &inc->count);
  if (tmp != 1) {
    printf("Bad input format (count isn't a uint64: ''%.*s'')\n", len, buffer);
    goto exit;
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Look at the reception timestamp field */
  if (!inc->origin) {
    tmp = sscanf(buffer, "%lf", &inc->timestamp);
    if (tmp != 1) {
      printf("Bad input format (reception timestamp isn't a double: ''%.*s'')\n", len, buffer);
      goto exit;
    }
  }

  return 1;
exit:
  printf("Error parsing input file %i (last count : %"PRIu64"\n", inc - in, inc->count);
  exit(-3);
}

static ssize_t
next_input(struct extract_io *inc)
{
  ssize_t tmp;
  do {
    tmp = read_input(inc);
  } while (tmp == 0);
  return tmp;
}

static void
synchronize_input()
{
  ssize_t tmp;
  double   ts;

  assert(MAX_SOURCES == 2);

  tmp = next_input(&in[0]);
  if (tmp < 0) {
    printf("End of file before any input for input file 0 (0-1) ...\n");
    exit(-4);
  }
  tmp = next_input(&in[1]);
  if (tmp < 0) {
    printf("End of file before any input for input file 1 (0-1)...\n");
    exit(-4);
  }
  while (1) {
    ts = in[0].timestamp - in[1].timestamp;
    if (ts > secure_interval) {
      tmp = next_input(&in[1]);
      if (tmp < 0) {
        printf("End of file before synchronisation for input file 1 (0-1)...\n");
        exit(-4);
      }
    } else if (ts < -secure_interval) {
      tmp = next_input(&in[0]);
      if (tmp < 0) {
        printf("End of file before synchronisation for input file 0 (0-1)...\n");
        exit(-4);
      }
    } else {
      break;
    }
  }
  sync_count_diff = in[0].count - in[1].count;
  in[0].last_count = in[0].count;
  in[1].last_count = in[1].count;
}

static int
next(FILE* out, bool print)
{
  ssize_t tmp;
  int64_t age[2];
  int64_t i;
  double   ts;

  age[0] = in[0].count - in[0].last_count;
  age[1] = in[1].count - in[1].last_count;

  assert((in[0].last_count - in[1].last_count) == sync_count_diff);
  if (age[0] == age[1]) {
    ts = in[0].timestamp - in[1].timestamp;
    if (abs(ts) > secure_interval) {
      printf("Desynchronisation between %"PRIu64" et %"PRIu64"\n", in[0].count, in[1].count);
      exit(4);
    }
    for (i = age[0]; i > 1; --i) {
      if (print) {
        fprintf(out, "0 0\n");
      }
      ++u64_stats[0][0];
    }
    if (print) {
      fprintf(out, "1 1\n");
    }
    ++u64_stats[1][1];
    tmp = next_input(&in[0]);
    if (tmp < 0) {
      return tmp;
    }
    tmp = next_input(&in[1]);
    return tmp;
  } else if (age[0] < age[1]) {
    for(i = age[0]; i > 1; --i) {
      if (print) {
        fprintf(out, "0 0\n");
      }
      ++u64_stats[0][0];
    }
    if (print) {
      fprintf(out, "1 0\n");
    }
    ++u64_stats[1][0];
    in[1].last_count += age[0];
    tmp = next_input(&in[0]);
    return tmp;
  } else /* age[0] > age[1] */ {
    for(i = age[1]; i > 1; --i) {
      if (print) {
        fprintf(out, "0 0\n");
      }
      ++u64_stats[0][0];
    }
      if (print) {
      fprintf(out, "0 1\n");
    }
    ++u64_stats[0][1];
    in[0].last_count += age[1];
    tmp = next_input(&in[1]);
    return tmp;
  }
}

static long double
lrs_part(uint64_t nij, uint64_t ni, uint64_t nj, uint64_t n)
{
  return ((long double) nij) * logl((((long double) n) * ((long double) nij)) / (((long double) ni) * ((long double) nj)));
}

static long double
pcs_part(uint64_t nij, uint64_t ni, uint64_t nj, uint64_t n)
{
  long double temp = (((long double) ni) * ((long double) nj)) / ((long double) n);
  long double square = ((long double) nij) - temp;

  return square * square / temp;
}

static void
print_stats()
{
  uint64_t partial_i[2];
  uint64_t partial_j[2];
  uint64_t total;
  long double lrs;
  long double pcs;
  int i,j;

  memset(partial_i, 0, sizeof(uint64_t[2]));
  memset(partial_j, 0, sizeof(uint64_t[2]));
  total = 0;
  lrs = 0;
  pcs = 0;

  for (i = 0; i < 2; ++i) {
    for (j = 0; j < 2; ++j) {
      total += u64_stats[i][j];
      partial_i[i] += u64_stats[i][j];
      partial_j[j] += u64_stats[i][j];
    }
  }

  for (i = 0; i < 2; ++i) {
    for (j = 0; j < 2; ++j) {
      lrs += lrs_part(u64_stats[i][j], partial_i[i], partial_j[j], total);
      pcs += pcs_part(u64_stats[i][j], partial_i[i], partial_j[j], total);
    }
  }

  printf("Statistics :\n");
  printf(" Ni,j:\n");
  for (i = 0; i < 2; ++i) {
    for (j = 0; j < 2; ++j) {
      printf("  N%i,%i = %"PRIu64"\n", i, j, u64_stats[i][j]);
    }
  }
  printf(" Ni,.:\n");
  printf("  N0,. = %"PRIu64"\n", partial_i[0]);
  printf("  N1,. = %"PRIu64"\n", partial_i[1]);
  printf(" N.,j:\n");
  printf("  N.,0 = %"PRIu64"\n", partial_j[0]);
  printf("  N.,1 = %"PRIu64"\n", partial_j[1]);
  printf(" Total : %"PRIu64"\n", total);
  /* Q = p (X > x) */
  printf(" LRS: p = %f\n", gsl_cdf_chisq_Q((double)(2 * lrs), 1));
  printf(" PCS: p = %f\n", gsl_cdf_chisq_Q((double)pcs, 1));
  printf(" 2lrs = %Lf, pcs = %Lf\n", 2 * lrs, pcs);
}


static void
usage(int error, char *name)
{
  printf("%s: Try to transform two inputs into 0s and 1s\n", name);
  printf("Usage: %s [OPTIONS]\n", name);
  printf("Options:\n");
  printf(" -h, --help           Print this ...\n");
  printf(" -o, --ouput  <file>  Specify the output file for the verbose sequence(default: no output)\n");
  printf(" -s, --stats          Output to the standard output the statistics of losses\n");
  printf(" -t, --time   <dur>   Specify the expected time slot duration in millisecond\n");
  printf(" -i, --input  <file>  Specify an output file (Need to be present %i times)\n", MAX_SOURCES);
  printf(" -f, --from   <addr>  Specify the source address to be analysed in the last file\n");
  printf("     --origin         Use the origin timestamp instead of the reception timestamp for the last file\n");

  exit(error);
}

static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"output",      required_argument, 0,  'o' },
  {"stats",             no_argument, 0,  's' },
  {"time",        required_argument, 0,  't' },
  {"input",       required_argument, 0,  'i' },
  {"from",        required_argument, 0,  'f' },
  {"origin",            no_argument, 0,  'a' },
  {NULL,                          0, 0,   0  }
};

int
main(int argc, char *argv[])
{
  int opt, ret, pos;
  FILE* tmp;
  char *out_filename = NULL;
  ssize_t sret;
  bool stats = false;
  bool print = false;

  pos = 0;
  secure_interval = 0;
  memset(in, 0, MAX_SOURCES * sizeof(struct extract_io));
  memset(u64_stats, 0, sizeof(uint64_t[2][2]));

  while((opt = getopt_long(argc, argv, "ho:st:i:f:a", long_options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage(0, argv[0]);
      case 'o':
        out_filename = optarg;
        break;
      case 's':
        if (stats) {
          printf("You cannot have more than one -s\n");
          usage(-2, argv[0]);
        }
        stats = true;
        break;
      case 't':
        if (secure_interval != 0) {
          printf("You can specify only one time slot duration\n");
          usage(-2, argv[0]);
        }
        ret = sscanf(optarg, "%lf", &secure_interval);
        if (ret != 1) {
          printf("Bad time slot format\n");
          return -2;
        }
        break;
      case 'i':
        if (pos >= MAX_SOURCES) {
          printf("Too much input files (max : %i)\n", MAX_SOURCES);
          usage(-2, argv[0]);
        }
        tmp = fopen(optarg, "r");
        if (tmp == NULL || ferror(tmp)) {
          printf("Unable to load %s\n", optarg);
          return -1;
        }
        ret = zinit_read(&in[pos].input, tmp);
        if (ret != 0) {
          printf("Unable to initialize zlib : %i\n", ret);
          printf("(-5 == not a .gz input file)\n");
          return -1;
        }
        ++pos;
        break;
      case 'f':
        if (pos < 1) {
          printf("-f option are not supposed to be before any -i option\n");
          usage(-2, argv[0]);
        }
        if (in[pos - 1].fixed_ip) {
          printf("Unable to have two different source addresses for the same flow\n");
          usage(-2, argv[0]);
        }
        assert(pos <= MAX_SOURCES);
        ret = inet_pton(AF_INET6, optarg, &in[pos - 1].src);
        assert(ret != -1);
        if (ret == 0) {
          printf("Invalid IPv6 address '%s'\n", optarg);
          return -2;
        }
        in[pos - 1].fixed_ip = true;
        break;
      case 'a':
        if (pos < 1) {
          printf("--origin option are not supposed to be before any -i option\n");
          usage(-2, argv[0]);
        }
        if (in[pos - 1].origin) {
          printf("Why did you put two --origin on the same -i ?\n");
          usage(-2, argv[0]);
        }
        assert(pos <= MAX_SOURCES);
        in[pos - 1].origin = true;
        break;
      default:
        usage(-1, argv[0]);
        break;
    }
  }

 if(argc > optind) {
    printf("Too much options\n");
    usage(-2, argv[0]);
    return 1;
  }

  if (pos < MAX_SOURCES) {
    printf("Not enough input files (%i < %i)\n", pos, MAX_SOURCES);
    usage(-2, argv[0]);
  }

  if (secure_interval == 0) {
    printf("No time slot duration specified, unable to synchronyse inputs\n");
    usage(-2, argv[0]);
  }
  secure_interval /= 2000;

  if (out_filename != NULL) {
    output = fopen(out_filename, "w");
    if (output == NULL) {
      printf("Unable to open output file\n");
      return -1;
    }
    print = true;
  } else if (!stats) {
    printf("No statistics required, no output file given: nothing to do, abording\n");
    usage(-2, argv[0]);
  }

  synchronize_input();
  printf("Synchronisation obtained at count: %"PRIu64", %"PRIu64"\n", in[0].count, in[1].count);
  do {
    sret = next(output, print);
  } while (sret >= 0);
  printf("End at count: %"PRIu64", %"PRIu64"\n", in[0].count, in[1].count);
  if (sret < -1) {
    printf("Error before EOF : %i\n", sret);
  }

  if (stats) {
    print_stats();
  }

  return 0;
}
