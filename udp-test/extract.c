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

/*
#ifdef DEBUG
#undef DEBUG
#endif
*/

#ifdef DEBUG
# include <signal.h>
#endif

/* Stupid dynamic structure */
#define LIST_STEP 7
struct array_list_u64 {
  uint64_t   data[LIST_STEP];
  struct array_list_u64 *next;
};

static void
increment_counter(struct array_list_u64 *list, uint64_t count)
{
  assert(list != NULL);
  if (count >= LIST_STEP) {
    if (list->next == NULL) {
      list->next = calloc(1, sizeof(struct array_list_u64));
      if (list->next == NULL) {
        printf("Calloc error\n");
        exit(-1);
      }
    }
    increment_counter(list->next, count - LIST_STEP);
  } else {
    list->data[count] += 1;
  }
}

/* File reading structs */
struct input_p
{
  char               *filename;
  int                 filename_count_start;
  int                 filename_count;
  struct zutil_read   input;
  struct in6_addr     src;
  bool                fixed_ip;
  bool                origin;
};

#define DELAY_BEFORE_RESYNCHRONISATION  15000

struct state
{
  struct input_p  input;
  uint64_t        count_new;
  uint64_t        count_old;
  double          timestamp;
  double          timestamp_old;
  int8_t          signal_new;
  int8_t          signal_old;
  uint64_t        desynchronization_drop_internal;
  uint64_t        desynchronization_drop_external;
  uint64_t        resynchronisation_counter;
  double          resynchronisation_min;
};

struct first_run
{
  uint32_t       histo;
  long double signal_m;
  long double signal_a;
  long double signal_b;
  long double signal_e;
  uint64_t  signal_m_c;
  uint64_t signal_ab_c;
  uint64_t  signal_e_c;
  uint64_t signal_strengh[UINT8_MAX + 1];
  struct array_list_u64 *bursts;
};

struct statistics {
  uint64_t partial_i[2];
  uint64_t partial_j[2];
  uint64_t total;
};

struct second_run
{
  void *null;
};

#ifdef DEBUG
struct state *states_for_interrupt;
#endif

/* Global cache */
#define SOURCES 2

uint64_t sync_count_diff;
double   interval;
double   secure_interval;

uint64_t u64_stats[2][2];
uint64_t *compare_histo;
struct array_list_u64 *coordbursts;
uint32_t histo_mod;
int k;
uint64_t signals[UINT8_MAX + 1][UINT8_MAX + 1];

struct historical_correlation {
  uint64_t data[4][3];
};
size_t long_history_size;
uint8_t *long_history,
        *long_history_current;
struct historical_correlation *histo_corr;
bool long_history_looped;

/* Output */
FILE* output;

static ssize_t
read_input(struct state *in_state)
{
  ssize_t len;
  char *buffer, *next;
  struct in6_addr ip;
  int tmp;
  double temp_ts, temp_diff;

  buffer = zread_line(&in_state->input.input, &len);
  if (buffer == NULL) {
    if (len < -1) {
      goto exit;
    }
    return -1;
  }
  assert(len >= 0);

  /* Verify the IP address */
  next = memchr(buffer, ',', (size_t)len);
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

  if (memcmp(&ip, &in_state->input.src, sizeof(struct in6_addr)) != 0) {
    if (in_state->input.fixed_ip) {
      return 0;
    }
    if (*in_state->input.src.s6_addr32 == 0) {
      memcpy(&in_state->input.src, &ip, sizeof(struct in6_addr));
    } else {
      printf("No from address was specified but two different addresses appeared\n");
      goto exit;
    }
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Skip the flag field */
  assert(len >= 0);
  next = memchr(buffer, ',', (size_t)len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the flag field : ''%.*s'')\n", len, buffer);
    goto exit;
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Store the signal strengh */
  assert(len >= 0);
  next = memchr(buffer, ',', (size_t)len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the signal field : ''%.*s'')\n", len, buffer);
    goto exit;
  }
  *next = '\0';
  in_state->signal_old = in_state->signal_new;
  tmp = sscanf(buffer, "%"SCNd8, &in_state->signal_new);
  if (tmp != 1) {
    printf("Bad input format (count isn't a int8: ''%.*s'')\n", len, buffer);
    goto exit;
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Skip the signal field */
  assert(len >= 0);
  next = memchr(buffer, ',', (size_t)len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the rate field : ''%.*s'')\n", len, buffer);
    goto exit;
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Look at the origin timestamp field */
  assert(len >= 0);
  next = memchr(buffer, ',', (size_t)len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the origin timestamp field : ''%.*s'')\n", len, buffer);
    goto exit;
  }
  if (in_state->input.origin) {
    *next = '\0';
    tmp = sscanf(buffer, "%lf", &in_state->timestamp);
    if (tmp != 1) {
      printf("Bad input format (origin timestamp isn't a double: ''%.*s'')\n", len, buffer);
      goto exit;
    }
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Read the count field */
  assert(len >= 0);
  next = memchr(buffer, ',', (size_t)len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the sent timestamp field : ''%.*s'')\n", len, buffer);
    goto exit;
  }
  *next = '\0';
  in_state->count_old = in_state->count_new;
  tmp = sscanf(buffer, "%"SCNd64, &in_state->count_new);
  if (tmp != 1) {
    printf("Bad input format (count isn't a uint64: ''%.*s'')\n", len, buffer);
    goto exit;
  }
  if ((in_state->count_new <= in_state->count_old) && (in_state->count_new != 0)) {
    printf("Bad input format (count isn't strictly increasing %"PRIu64" after %"PRIu64")\n", in_state->count_new, in_state->count_old);
    goto exit;
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Look at the reception timestamp field */
  if (!in_state->input.origin) {
    tmp = sscanf(buffer, "%lf", &in_state->timestamp);
    if (tmp != 1) {
      printf("Bad input format (reception timestamp isn't a double: ''%.*s'')\n", len, buffer);
      goto exit;
    }
  }

  if (in_state->timestamp_old != 0) {
    temp_ts = in_state->timestamp_old + interval * (double)(in_state->count_new - in_state->count_old);
    if (temp_ts - in_state->timestamp > secure_interval) {
       /* We are correcting this kind of drift at each step, thus this should not happen */
       printf("Algorithmic error: a packet arrived too early\n");
       goto exit;
    } else if (in_state->timestamp - temp_ts > interval) {
      PRINTF("File %s, ", in_state->input.filename);
      PRINTF("packet %"PRIu64" dropped because outside its window (%lf VS %lf)\n", in_state->count_new, in_state->timestamp, temp_ts)
      in_state->signal_new = in_state->signal_old;
      in_state->count_new = in_state->count_old;
      ++in_state->desynchronization_drop_internal;
      return 0;
    } else {
      if (in_state->timestamp <= temp_ts) {
        PRINTF("Resynchronisation @%"PRIu64": %f\n", in_state->count_new,  in_state->timestamp - temp_ts)
        in_state->timestamp_old = in_state->timestamp;
        in_state->resynchronisation_counter = 0;
        in_state->resynchronisation_min = interval;
      } else {
        ++in_state->resynchronisation_counter;
        temp_diff = in_state->timestamp - temp_ts;
        if (temp_diff < in_state->resynchronisation_min) {
          in_state->resynchronisation_min = temp_diff;
        }
        if (in_state->resynchronisation_counter > DELAY_BEFORE_RESYNCHRONISATION) {
          PRINTF("Resynchronisation @%"PRIu64": +%f\n", in_state->count_new,  in_state->resynchronisation_min);
          in_state->timestamp_old = temp_ts + in_state->resynchronisation_min;
          in_state->resynchronisation_counter = 0;
          in_state->resynchronisation_min = interval;
        } else {
          in_state->timestamp_old = temp_ts;
        }
      }
    }
  }

  return 1;
exit:
  printf("Error parsing input file %s (last count : %"PRIu64"\n)", in_state->input.filename, in_state->count_new);
  exit(-3);
}

static ssize_t
next_input(struct state *inc)
{
  ssize_t tmp;
  do {
    tmp = read_input(inc);
  } while (tmp == 0);
  return tmp;
}

static void
synchronize_input(struct state* states)
{
  ssize_t tmp;
  double   ts;

#if SOURCES != 2
# error "This function was written only for 2 sources"
#endif

  tmp = next_input(states);
  if (tmp < 0) {
    printf("End of file before any input for input file 0 (0-1) ...\n");
    exit(-4);
  }
  tmp = next_input(states + 1);
  if (tmp < 0) {
    printf("End of file before any input for input file 1 (0-1)...\n");
    exit(-4);
  }
  while (1) {
    ts = states[0].timestamp - states[1].timestamp;
    if (ts > secure_interval) {
      tmp = next_input(states + 1);
      if (tmp < 0) {
        printf("End of file before synchronisation for input file 1 (0-1)...\n");
        exit(-4);
      }
    } else if (ts < -secure_interval) {
      tmp = next_input(states);
      if (tmp < 0) {
        printf("End of file before synchronisation for input file 0 (0-1)...\n");
        exit(-4);
      }
    } else {
      break;
    }
  }
  sync_count_diff = states[0].count_new - states[1].count_new;
  states[0].count_old = states[0].count_new;
  states[0].timestamp_old = states[0].timestamp;
  states[1].count_old = states[1].count_new;
  states[1].timestamp_old = states[1].timestamp;
}

static int
next_line_or_file(struct state *in_state)
{
  ssize_t tmp;
  size_t len;
  int ret;
  FILE *src;

  tmp = next_input(in_state);
  if (tmp != -1) {
    return tmp;
  }

  if (in_state->input.filename_count_start < 0) {
    return -1;
  }
  ++in_state->input.filename_count;
  if (in_state->input.filename_count > 1000) {
    return -1;
  }

  len = strlen(in_state->input.filename);
  assert(len > 7);
  snprintf(in_state->input.filename + (len - 6), 7, "%03i.gz", in_state->input.filename_count);

  PRINTF("Using next file, %s: ", in_state->input.filename);
  /* Try to open the new file */
  src = fopen(in_state->input.filename, "r");
  if (src == NULL) {
    PRINTF("No such file\n");
    return -1;
  }

  zread_end(&in_state->input.input);
  memset(&in_state->input.input, 0, sizeof(struct zutil_read));
  ret = zinit_read(&in_state->input.input, src);
  if (ret < 0) {
    printf("Zlib encoding error or no dat\n");
    return ret;
  }
  PRINTF("Success\n");
  return next_line_or_file(in_state);
}

inline static void
temporal_dependence(uint8_t new_state)
{
  uint8_t *current, *end;
  struct historical_correlation *histo_current;

  if (long_history_looped && (new_state != 0b11)) {
    histo_current = histo_corr;

    end = long_history + long_history_size;
    current = long_history_current + 1;

    assert(long_history <= current);
    assert(current <= end);

    while (current < end) {
      ++histo_current->data[*current][new_state];
      ++histo_current;
      ++current;
    }

    current = long_history;
    end = long_history_current + 1;

    while (current < end) {
      ++histo_current->data[*current][new_state];
      ++histo_current;
      ++current;
    }
  }

  *long_history_current = new_state;

  --long_history_current;
  if (long_history_current < long_history) {
    long_history_looped = true;
    long_history_current = long_history + long_history_size - 1;
  }
}

#define ADD_VAL(a,b)                                          \
  if (compare_histo != NULL) {                                \
    data[0].histo = ((data[0].histo << 1) + a) % histo_mod;   \
    data[1].histo = ((data[1].histo << 1) + b) % histo_mod;   \
    compare_histo[(data[0].histo << k) + data[1].histo] += 1; \
  }                                                           \
  ++u64_stats[a][b];

#define ADD_VAR_ONE(a)         \
  if (long_history != NULL) {  \
    temporal_dependence(a);    \
  }


#define ADD_BURST(dest,size)                 \
  if (dest != NULL) {                        \
    ({                                       \
      uint64_t count_ = size - 1;            \
      if (count_ > 0) {                      \
        increment_counter(dest, count_ - 1); \
      }                                      \
    });                                      \
  }

#define READ_LINE(pos)                                                       \
  tmp = next_line_or_file(states + pos);                                     \
  if (tmp < 0) {                                                             \
    return tmp;                                                              \
  }                                                                          \
  ADD_BURST(data[pos].bursts, states[pos].count_new - states[pos].count_old) \
  if (states[pos].count_new - states[pos].count_old > 1) {                   \
    data[pos].signal_b += states[pos].signal_old;                            \
    data[pos].signal_a += states[pos].signal_new;                            \
    ++data[pos].signal_ab_c;                                                 \
  }                                                                          \
  data[pos].signal_strengh[(uint8_t)states[pos].signal_new] += 1;

static int
first_pass(FILE* out, bool print, struct first_run *data, struct state *states)
{
  ssize_t tmp;
  uint64_t age[2];
  uint64_t i;
  double   ts;

  assert(states[0].count_new >= states[0].count_old);
  age[0] = states[0].count_new - states[0].count_old;
  assert(states[1].count_new >= states[1].count_old);
  age[1] = states[1].count_new - states[1].count_old;

  assert((states[0].count_old - states[1].count_old) == sync_count_diff);
  if (age[0] == age[1]) {
    ts = states[0].timestamp - states[1].timestamp;
    if (fabs(ts) > interval) {
      if (fabs(states[0].timestamp_old - states[1].timestamp) > interval) {
        ++states[1].desynchronization_drop_external;
        PRINTF("File %s, ", states[1].input.filename);
        PRINTF("packet %"PRIu64" dropped because outside the global window (%lf VS %lf)\n", states[1].count_new, states[1].timestamp, states[0].timestamp_old);
        states[1].timestamp_old -= interval * (double)(states[1].count_new - states[1].count_old);
        states[1].signal_new = states[1].signal_old;
        states[1].count_new = states[1].count_old;
        return next_line_or_file(states + 1);
      } else if (fabs(states[1].timestamp_old - states[0].timestamp) > interval) {
        ++states[0].desynchronization_drop_external;
        PRINTF("File %s, ", states[0].input.filename);
        PRINTF("packet %"PRIu64" dropped because outside the global window (%lf VS %lf)\n", states[0].count_new, states[0].timestamp, states[1].timestamp_old);
        states[0].timestamp_old -= interval * (double)(states[0].count_new - states[0].count_old);
        states[0].signal_new = states[0].signal_old;
        states[0].count_new = states[0].count_old;
        return next_line_or_file(states);
      } else {
        printf("Desynchronisation between %"PRIu64" and %"PRIu64"\n", states[0].count_new, states[1].count_new);
        printf("current: %lf - %lf -> %lf (VS %lf)\n", states[0].timestamp, states[1].timestamp, fabs(ts), interval);
        printf("ref:     %lf - %lf  \n", states[0].timestamp_old, states[1].timestamp_old);
        printf("(%"PRIu64" and %"PRIu64")\n",states[0].count_old, states[1].count_old);
        exit(4);
      }
    }
    if (age[0] != 0) {
      for (i = age[0]; i > 1; --i) {
        if (print) {
          fprintf(out, "0 0\n");
        }
        ADD_VAL(0,0)
        ADD_VAR_ONE(0b00)
        ++signals[INT8_MAX + 1][INT8_MAX + 1];
      }
      ADD_BURST(coordbursts, age[0])
      if (print) {
        fprintf(out, "1 1 | %"PRIi8" - %"PRIi8"\n", states[0].signal_new, states[1].signal_new);
      }
      ADD_VAL(1,1)
      ADD_VAR_ONE(0b11)
      ++signals[(uint8_t)states[0].signal_new][(uint8_t)states[1].signal_new];
      data[0].signal_m += states[0].signal_new;
      data[1].signal_m += states[1].signal_new;
      ++data[0].signal_m_c;
      ++data[1].signal_m_c;
    }
    READ_LINE(0)
    READ_LINE(1)
    return tmp;
  } else if (age[0] < age[1]) {
    for(i = age[0]; i > 1; --i) {
      if (print) {
        fprintf(out, "0 0\n");
      }
      ADD_VAL(0,0)
      ADD_VAR_ONE(0b00)
      ++signals[INT8_MAX + 1][INT8_MAX + 1];
    }
    ADD_BURST(coordbursts, age[0])
    if (print) {
      fprintf(out, "1 0 | %"PRIi8" - %"PRIi8"\n", states[0].signal_new, 0);
    }
    ADD_VAL(1,0)
    ADD_VAR_ONE(0b10)
    ++signals[(uint8_t)states[0].signal_new][INT8_MAX + 1];
    data[0].signal_e += states[0].signal_new;
    ++data[0].signal_e_c;
    ++data[1].signal_m_c;
    states[1].count_old += age[0];
    READ_LINE(0)
    return tmp;
  } else /* age[0] > age[1] */ {
    for(i = age[1]; i > 1; --i) {
      if (print) {
        fprintf(out, "0 0\n");
      }
      ADD_VAL(0,0)
      ADD_VAR_ONE(0b00)
      ++signals[INT8_MAX + 1][INT8_MAX + 1];
    }
    ADD_BURST(coordbursts, age[1])
    if (print) {
      fprintf(out, "0 1 | %"PRIi8" - %"PRIi8"\n", 0, states[1].signal_new);
    }
    ADD_VAL(0,1)
    ADD_VAR_ONE(0b01)
    ++signals[INT8_MAX + 1][(uint8_t)states[1].signal_new];
    data[1].signal_e += states[1].signal_new;
    ++data[1].signal_e_c;
    ++data[0].signal_m_c;
    states[0].count_old += age[1];
    READ_LINE(1)
    return tmp;
  }
}

#undef ADD_VAR_ONE
#undef ADD_VAL
#undef READ_LINE

static int
second_pass(FILE* out, bool print, struct second_run *data, struct state *states)
{
  ssize_t tmp;
  uint64_t age[2];
  uint64_t i;
  double   ts;

  assert(states[0].count_new >= states[0].count_old);
  age[0] = states[0].count_new - states[0].count_old;
  assert(states[1].count_new >= states[1].count_old);
  age[1] = states[1].count_new - states[1].count_old;

  assert((states[0].count_old - states[1].count_old) == sync_count_diff);
  if (age[0] == age[1]) {
    ts = states[0].timestamp - states[1].timestamp;
    if (fabs(ts) > secure_interval) {
      printf("Desynchronisation between %"PRIu64" et %"PRIu64"\n", states[0].count_new, states[1].count_new);
      printf("current: %lf - %lf -> %lf (VS %lf)\n", states[0].timestamp, states[1].timestamp, fabs(ts), secure_interval);
      printf("ref:     %lf - %lf\n", states[0].timestamp_old, states[1].timestamp_old);
      exit(4);
    }
    if (age[0] != 0) {
      for (i = age[0]; i > 1; --i) {

      }

    }
    tmp = next_line_or_file(states);
    if (tmp < 0) {
      return tmp;
    }
    tmp = next_line_or_file(states + 1);
    return tmp;
  } else if (age[0] < age[1]) {
    for(i = age[0]; i > 1; --i) {

    }

    states[1].count_old += age[0];
    tmp = next_line_or_file(states);
    return tmp;
  } else /* age[0] > age[1] */ {
    for(i = age[1]; i > 1; --i) {

    }

    states[0].count_old += age[1];
    tmp = next_line_or_file(states + 1);
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

static struct statistics*
eval_stats(struct first_run *data)
{
  struct statistics *ret;
  int i,j;

  ret = calloc(1, sizeof(struct statistics));
  if (ret == NULL) {
    printf("Malloc Error\n");
    exit(-1);
  }

  for (i = 0; i < 2; ++i) {
    for (j = 0; j < 2; ++j) {
      ret->total += u64_stats[i][j];
      ret->partial_i[i] += u64_stats[i][j];
      ret->partial_j[j] += u64_stats[i][j];
    }
  }

  return ret;
}

static void
print_desynchronisation_stats(struct state *states)
{
  printf("Desynchronisation drops :\n");
  printf(" Internals: %"PRIu64" and %"PRIu64"\n", states[0].desynchronization_drop_internal, states[1].desynchronization_drop_internal);
  printf(" Externals: %"PRIu64" and %"PRIu64"\n", states[0].desynchronization_drop_external, states[1].desynchronization_drop_external);
}

static void
print_stats(struct first_run *data, struct statistics* stats)
{
  long double lrs;
  long double pcs;
  int i,j;
  struct array_list_u64 *temp;

  lrs = 0;
  pcs = 0;

  for (i = 0; i < 2; ++i) {
    for (j = 0; j < 2; ++j) {
      lrs += lrs_part(u64_stats[i][j], stats->partial_i[i], stats->partial_j[j], stats->total);
      pcs += pcs_part(u64_stats[i][j], stats->partial_i[i], stats->partial_j[j], stats->total);
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
  printf("  N0,. = %"PRIu64"\n", stats->partial_i[0]);
  printf("  N1,. = %"PRIu64"\n", stats->partial_i[1]);
  printf(" N.,j:\n");
  printf("  N.,0 = %"PRIu64"\n", stats->partial_j[0]);
  printf("  N.,1 = %"PRIu64"\n", stats->partial_j[1]);
  printf(" Total : %"PRIu64"\n", stats->total);
  /* Q = p (X > x) */
  printf(" LRS: p = %f\n", gsl_cdf_chisq_Q((double)(2 * lrs), 1));
  printf(" PCS: p = %f\n", gsl_cdf_chisq_Q((double)pcs, 1));
  printf(" 2lrs = %Lf, pcs = %Lf\n", 2 * lrs, pcs);

  printf(" Bursts\n");
  for (i = 0; i < 2; ++i) {
    printf("  %i: [", i);
    temp = data[i].bursts;
    while (temp->next != NULL) {
      for (j = 0; j < LIST_STEP; ++j) {
        printf("%"PRIu64" ", temp->data[j]);
      }
      temp = temp->next;
    }
    for (j = 0; j < LIST_STEP - 1; ++j) {
      printf("%"PRIu64" ", temp->data[j]);
    }
    printf("%"PRIu64"]\n", temp->data[LIST_STEP - 1]);
  }
  printf("  Both : [");
  temp = coordbursts;
  while (temp->next != NULL) {
    for (j = 0; j < LIST_STEP; ++j) {
      printf("%"PRIu64" ", temp->data[j]);
    }
    temp = temp->next;
  }
  for (j = 0; j < LIST_STEP - 1; ++j) {
    printf("%"PRIu64" ", temp->data[j]);
  }
  printf("%"PRIu64"]\n", temp->data[LIST_STEP - 1]);

  printf("Estimation (N0,0) : %lf (VS %"PRIu64")\n", ((double)stats->partial_i[0]) / ((double)stats->total) * ((double)stats->partial_j[0]), u64_stats[0][0]);

  printf("Simple model (0,. and .,0 independant, common error q):\n");

  long double x,y,z;
  x = u64_stats[0][0] / ((long double) stats->total);
  y = u64_stats[0][1] / ((long double) stats->total);
  z = u64_stats[1][0] / ((long double) stats->total);

  printf(" 0,. = %Lf\n", y / (1 - x - z));
  printf(" .,0 = %Lf\n", z / (1 - x - y));
  printf(" q = %Lf\n", (x * y + z * y + z * x + x * x - x) / (x + y + z - 1));

  printf("(Co)variance and Pearson correlation\n");

  long double mean[2], stand_dev[2], covar;
  mean[0] = ((long double)stats->partial_i[1]) / ((long double)stats->total);
  mean[1] = ((long double)stats->partial_j[1]) / ((long double)stats->total);
  stand_dev[0] = sqrtl((mean[0] * mean[0] * ((long double)stats->partial_i[0]) + (1 - mean[0]) * (1 - mean[0]) * ((long double)stats->partial_i[1])) / ((long double)stats->total));
  stand_dev[1] = sqrtl((mean[1] * mean[1] * ((long double)stats->partial_j[0]) + (1 - mean[1]) * (1 - mean[1]) * ((long double)stats->partial_j[1])) / ((long double)stats->total));
  covar = (0 - mean[0]) * (0 - mean[1]) * u64_stats[0][0] + (1 - mean[0]) * (0 - mean[1]) * u64_stats[1][0] + (0 - mean[0]) * (1 - mean[1]) * u64_stats[0][1] + (1 - mean[0]) * (1 - mean[1]) * u64_stats[1][1];
  printf(" I : %Lf\n", stand_dev[0]);
  printf(" J : %Lf\n", stand_dev[1]);
  printf(" Cov : %Lf\n", covar / stats->total);
  printf("Pearson correlation : %Lf\n", covar / stats->total / (stand_dev[0] * stand_dev[1]));
}

static void
print_signal_stats(FILE *signal_output, struct first_run *data)
{
  int i,j;

  for (i = 0; i < 2; ++i) {
    data[i].signal_e /= (data[i].signal_e_c);
    data[i].signal_m /= (data[i].signal_m_c);
    data[i].signal_b /= (data[i].signal_ab_c);
    data[i].signal_a /= (data[i].signal_ab_c);
  }

  fprintf(signal_output, "Signal strengh:\n");
  fprintf(signal_output, "  Average : %Lf - %Lf  (%"PRIu64"-%"PRIu64")\n", data[0].signal_m, data[1].signal_m, data[0].signal_m_c, data[1].signal_m_c);
  fprintf(signal_output, "  Error on the other side: %Lf - %Lf  (%"PRIu64"-%"PRIu64")\n", data[0].signal_e, data[1].signal_e, data[0].signal_e_c, data[1].signal_e_c);
  fprintf(signal_output, "  Before error: %Lf - %Lf  (%"PRIu64"-%"PRIu64")\n", data[0].signal_b, data[1].signal_b, data[0].signal_ab_c, data[1].signal_ab_c);
  fprintf(signal_output, "  After error: %Lf - %Lf  (%"PRIu64"-%"PRIu64")\n", data[0].signal_a, data[1].signal_a, data[0].signal_ab_c, data[1].signal_ab_c);
  fprintf(signal_output, " Graph:\n");
  fprintf(signal_output, "[");
  for (i = INT8_MAX + 1; i < UINT8_MAX; ++i) {
    fprintf(signal_output, "%"PRIi8" ", (int8_t)i);
  }
  fprintf(signal_output, "%"PRIi8"]\n", (int8_t)UINT8_MAX);
  for (i = 0; i < 2; ++i) {
    fprintf(signal_output, "[");
    for (j = INT8_MAX + 1; j < UINT8_MAX; ++j) {
      fprintf(signal_output, "%"PRIu64" ", data[i].signal_strengh[j]);
    }
    fprintf(signal_output, "%"PRIu64"]\n", data[i].signal_strengh[UINT8_MAX]);
  }
  fprintf(signal_output, " Signal Matrix:\n");
  fprintf(signal_output, "  (Axis : [");
  for (i = INT8_MAX + 1; i < UINT8_MAX; ++i) {
    fprintf(signal_output, "%"PRIi8" ", (int8_t)i);
  }
  fprintf(signal_output, "%"PRIi8"])\n", (int8_t)UINT8_MAX);
  fprintf(signal_output, "[");
  for (i = INT8_MAX + 1; i < UINT8_MAX; ++i) {
    for (j = INT8_MAX + 1; j < UINT8_MAX; ++j) {
      fprintf(signal_output, "%"PRIu64" ", signals[i][j]);
    }
    fprintf(signal_output, "%"PRIu64"; ", signals[i][UINT8_MAX]);
  }
  for (j = INT8_MAX + 1; j < UINT8_MAX; ++j) {
    fprintf(signal_output, "%"PRIu64" ", signals[UINT8_MAX][j]);
  }
  fprintf(signal_output, "%"PRIu64"]\n", signals[UINT8_MAX][UINT8_MAX]);
}

static void
print_histo(FILE *histo_output)
{
  uint32_t i,j;
  uint64_t max, total;
  uint64_t *independant;

  fprintf(histo_output, "[");
  for (i = 0; i < histo_mod - 1; ++i) {
    for (j = 0; j < histo_mod - 1; ++j) {
      fprintf(histo_output, "%"PRIu64" ", compare_histo[(i * histo_mod) + j]);
    }
    fprintf(histo_output, "%"PRIu64";", compare_histo[(i * histo_mod) + histo_mod - 1]);
  }
  for (j = 0; j < histo_mod - 1; ++j) {
    fprintf(histo_output, "%"PRIu64" ", compare_histo[((histo_mod - 1) * histo_mod) + j]);
  }
  fprintf(histo_output, "%"PRIu64"]\n", compare_histo[((histo_mod - 1) * histo_mod) + histo_mod - 1]);

  fprintf(histo_output, "Log():\n");
  fprintf(histo_output, "[");
  for (i = 0; i < histo_mod - 1; ++i) {
    for (j = 0; j < histo_mod - 1; ++j) {
      fprintf(histo_output, "%Lf ", logl(1 + compare_histo[(i * histo_mod) + j]));
    }
    fprintf(histo_output, "%Lf;", logl(1 + compare_histo[(i * histo_mod) + histo_mod - 1]));
  }
  for (j = 0; j < histo_mod - 1; ++j) {
    fprintf(histo_output, "%Lf ", logl(1 + compare_histo[((histo_mod - 1) * histo_mod) + j]));
  }
  fprintf(histo_output, "%Lf]\n", logl(1 + compare_histo[((histo_mod - 1) * histo_mod) + histo_mod - 1]));

  max = 10 * compare_histo[((histo_mod - 1)/2 * (histo_mod + 1))];
  fprintf(histo_output, "Limit at %"PRIi64"\n", max);
#define LIMIT_MAX_VAL(x) ({ uint64_t val_ = x; (val_ > max) ? (-1) : ((int64_t)val_); })
  fprintf(histo_output, "[");
  for (i = 0; i < histo_mod - 1; ++i) {
    for (j = 0; j < histo_mod - 1; ++j) {
      fprintf(histo_output, "%"PRIi64" ", LIMIT_MAX_VAL(compare_histo[(i * histo_mod) + j]));
    }
    fprintf(histo_output, "%"PRIi64";", LIMIT_MAX_VAL(compare_histo[(i * histo_mod) + histo_mod - 1]));
  }
  for (j = 0; j < histo_mod - 1; ++j) {
    fprintf(histo_output, "%"PRIi64" ", LIMIT_MAX_VAL(compare_histo[((histo_mod - 1) * histo_mod) + j]));
  }
  fprintf(histo_output, "%"PRIi64"]\n", LIMIT_MAX_VAL(compare_histo[((histo_mod - 1) * histo_mod) + histo_mod - 1]));
#undef LIMIT_MAX_VAL

  fprintf(histo_output, "Trying to visualize difference with independant variables:\n");
  assert((k >= 0) && (k <= 15));
  independant = calloc(((size_t)1) << (k + 1), sizeof(uint64_t));
  total = 0;
  for (i = 0; i < histo_mod; ++i) {
    for (j = 0; j < histo_mod; ++j) {
      independant[i] += compare_histo[(i * histo_mod) + j];
      independant[j + histo_mod] += compare_histo[(i * histo_mod) + j];
      total += compare_histo[(i * histo_mod) + j];
    }
  }
  fprintf(histo_output, " Independant:\n");

#define INDEP(i,j)  ((uint64_t)(((long double)(independant[i] * independant[j + histo_mod])) / total))
  fprintf(histo_output, "  [");
  for (i = 0; i < histo_mod - 1; ++i) {
    for (j = 0; j < histo_mod - 1; ++j) {
      fprintf(histo_output, "%"PRIi64" ", INDEP(i,j));
    }
    fprintf(histo_output, "%"PRIi64";", INDEP(i,histo_mod - 1));
  }
  for (j = 0; j < histo_mod - 1; ++j) {
    fprintf(histo_output, "%"PRIi64" ", INDEP(histo_mod - 1, j));
  }
  fprintf(histo_output, "%"PRIi64"]\n", INDEP(histo_mod - 1, histo_mod - 1));

  fprintf(histo_output, " Diff:\n");
#define INDEP_DIFF(i,j)  ((int64_t)(compare_histo[((i) * histo_mod) + j] - INDEP(i,j)))
  fprintf(histo_output, "  [");
  for (i = 0; i < histo_mod - 1; ++i) {
    for (j = 0; j < histo_mod - 1; ++j) {
      fprintf(histo_output, "%"PRIi64" ", INDEP_DIFF(i,j));
    }
    fprintf(histo_output, "%"PRIi64";", INDEP_DIFF(i,histo_mod - 1));
  }
  for (j = 0; j < histo_mod - 1; ++j) {
    fprintf(histo_output, "%"PRIi64" ", INDEP_DIFF(histo_mod - 1, j));
  }
  fprintf(histo_output, "%"PRIi64"]\n", INDEP_DIFF(histo_mod - 1, histo_mod - 1));

  fprintf(histo_output, " Log diff:\n");
#define SIGNED_LOG(a) ({ int64_t a_ = a; a_ >= 0 ? logl(1 + (long double)a_) : -logl(-(long double)a_); })
  fprintf(histo_output, "  [");
  for (i = 0; i < histo_mod - 1; ++i) {
    for (j = 0; j < histo_mod - 1; ++j) {
      fprintf(histo_output, "%Lf ", SIGNED_LOG(INDEP_DIFF(i,j)));
    }
    fprintf(histo_output, "%Lf;", SIGNED_LOG(INDEP_DIFF(i,histo_mod - 1)));
  }
  for (j = 0; j < histo_mod - 1; ++j) {
    fprintf(histo_output, "%Lf ", SIGNED_LOG(INDEP_DIFF(histo_mod - 1, j)));
  }
  fprintf(histo_output, "%Lf]\n", SIGNED_LOG(INDEP_DIFF(histo_mod - 1, histo_mod - 1)));

#undef SIGNED_LOG
#undef INDEP_DIFF
#undef INDEP
  free(independant);
}

static void
print_histo_correlation(FILE *histo_corr_file, struct statistics* stats)
{
  size_t i;
  long double temp;

#define PRINT_SEP                 \
  fprintf(histo_corr_file, ", ");

#define PRINT_AXIS                                          \
  fprintf(histo_corr_file, "[1:1:%zu]", long_history_size);

#define PRINT_PLOT_START(a,b)                             \
  fprintf(histo_corr_file, "%% %s:\n", a);                \
  fprintf(histo_corr_file, "figure('Name','%s');\n", b);  \
  fprintf(histo_corr_file, "plot(");

#define PRINT_PLOT_LINE_INFO(a)                        \
  fprintf(histo_corr_file, ", '%s'", a);

#define PRINT_REFERENCE(a,b)                   \
  temp = a;                                    \
  PRINT_AXIS                                   \
  fprintf(histo_corr_file, ", [");             \
  for (i = 0; i < long_history_size - 1; ++i)  \
    fprintf(histo_corr_file, "%Lf ", temp);    \
  fprintf(histo_corr_file, "%Lf]", temp);      \
  PRINT_PLOT_LINE_INFO(b)
//"

#define PRINT_CURVE(a,b,c,d)                                                                            \
  PRINT_AXIS                                                                                            \
  fprintf(histo_corr_file, ", [");                                                                      \
  for (i = 0; i < long_history_size - 1; ++i)                                                           \
    fprintf(histo_corr_file, "%Lf ", histo_corr[i].data[a][b] / ((long double) c));                     \
  fprintf(histo_corr_file, "%Lf]", histo_corr[long_history_size - 1].data[a][b] / ((long double) c));   \
  PRINT_PLOT_LINE_INFO(d)
//"

#define PRINT_CURVE_SUM(a,b,c,d)                                                                                                                          \
  PRINT_AXIS                                                                                                                                              \
  fprintf(histo_corr_file, ", [");                                                                                                                        \
  for (i = 0; i < long_history_size - 1; ++i)                                                                                                             \
    fprintf(histo_corr_file, "%Lf ", (histo_corr[i].data[a][b] + histo_corr[i].data[a][0b00]) / ((long double) c));                                       \
  fprintf(histo_corr_file, "%Lf]", (histo_corr[long_history_size - 1].data[a][b] + histo_corr[long_history_size - 1].data[a][0b00]) / ((long double) c)); \
  PRINT_PLOT_LINE_INFO(d)
//"

#define PRINT_CURVE_DOUBLE_SUM(a,b,c,d)                                                                                                                                                              \
  PRINT_AXIS                                                                                                                                                                                         \
  fprintf(histo_corr_file, ", [");                                                                                                                                                                   \
  for (i = 0; i < long_history_size - 1; ++i)                                                                                                                                                        \
    fprintf(histo_corr_file, "%Lf ", (histo_corr[i].data[a][b] + histo_corr[i].data[a][0b00] + histo_corr[i].data[0b00][b] + histo_corr[i].data[0b00][0b00]) / ((long double) c + u64_stats[0][0])); \
  i = long_history_size - 1;                                                                                                                                                                         \
  fprintf(histo_corr_file, "%Lf]", (histo_corr[i].data[a][b] + histo_corr[i].data[a][0b00] + histo_corr[i].data[0b00][b] + histo_corr[i].data[0b00][0b00]) / ((long double) c + u64_stats[0][0]));   \
  PRINT_PLOT_LINE_INFO(d)
//"

#define PRINT_PLOT_END(a)                       \
  fprintf(histo_corr_file, ");\n");             \
  fprintf(histo_corr_file, "legend(%s);\n", a); \
  fprintf(histo_corr_file, "\n");

  PRINT_PLOT_START("green/blue: 01|01; magenta/red: 10|10", "Autocorelation (First order loss)")
  PRINT_REFERENCE(u64_stats[0][1] / ((long double)stats->total), "b")
  PRINT_SEP
  PRINT_CURVE(0b01, 0b01, u64_stats[0][1], "g")
  PRINT_SEP
  PRINT_REFERENCE(u64_stats[1][0] / ((long double)stats->total), "r")
  PRINT_SEP
  PRINT_CURVE(0b10, 0b10, u64_stats[1][0], "m")
  PRINT_PLOT_END("'ref 01','01|01','ref 10','10|10'")

  PRINT_PLOT_START("red/black: 00|00", "Autocorelation (Second order loss)")
  PRINT_REFERENCE(u64_stats[0][0] / ((long double)stats->total), "k")
  PRINT_SEP
  PRINT_CURVE(0b00,0b00, u64_stats[0][0], "r")
  PRINT_PLOT_END("'ref 00','00|00'");

  PRINT_PLOT_START("green/blue: 10|01; magenta/red: 01|10", "Correlation (First order loss)")
  PRINT_REFERENCE(u64_stats[1][0] / ((long double)stats->total), "b")
  PRINT_SEP
  PRINT_CURVE(0b01, 0b10, u64_stats[0][1], "g")
  PRINT_SEP
  PRINT_REFERENCE(u64_stats[0][1] / ((long double)stats->total), "r")
  PRINT_SEP
  PRINT_CURVE(0b10, 0b01, u64_stats[1][0], "m")
  PRINT_PLOT_END("'ref 10','10|01','ref 01','01|10'")

  PRINT_PLOT_START("green/black: 00|01; red/black: 00|10", "Corelation (Second order loss)")
  PRINT_REFERENCE(u64_stats[0][0] / ((long double)stats->total), "k")
  PRINT_SEP
  PRINT_CURVE(0b01, 0b00, u64_stats[0][1], "g")
  PRINT_SEP
  PRINT_CURVE(0b10, 0b00, u64_stats[1][0], "r")
  PRINT_PLOT_END("'ref 00','00|01','00|10'")

  PRINT_PLOT_START("green/blue: 0.|0.; magenta/red: .0|.0", "Autocorelation (Independant loss)")
  PRINT_REFERENCE((u64_stats[0][1] + u64_stats[0][0]) / ((long double)stats->total), "b")
  PRINT_SEP
  PRINT_CURVE_DOUBLE_SUM(0b01, 0b01, u64_stats[0][1], "g")
  PRINT_SEP
  PRINT_REFERENCE((u64_stats[1][0] + u64_stats[0][0]) / ((long double)stats->total), "r")
  PRINT_SEP
  PRINT_CURVE_DOUBLE_SUM(0b10, 0b10, u64_stats[1][0], "m")
  PRINT_PLOT_END("'ref 0.','0.|0.','ref .0','.0|.0'")

#undef PRINT_PLOT_END
#undef PRINT_CURVE_DOUBLE_SUM
#undef PRINT_CURVE
#undef PRINT_REFERENCE
#undef PRINT_PLOT_START
#undef PRINT_AXIS
#undef PRINT_SEP
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
  printf(" -i, --input  <file>  Specify an output file (Need to be present %i times)\n", SOURCES);
  printf(" -f, --from   <addr>  Specify the source address to be analysed in the last file\n");
  printf("     --origin         Use the origin timestamp instead of the reception timestamp for the last file\n");
  printf(" -r, --rotated        The input file was rotated, use all the rotated files\n");
  printf(" -k           <pow>   Size of the stored log (used for compairing sequences), expressed in 2 << <pow>\n");
  printf(" -q, --histfile <f>   Name of the file used for the output of the comparaison of sequences\n");
  printf(" -p, --signal=[file]  Turn on the output of signal related statistics. If [file] is specified, use [file] for the output. Use the standard output by default\n");
  printf(" --temp_corr_s <size> Size of the history for the graphs for temporal correlation (default: disabled)\n");
  printf(" --temp_corr_f <file> File for the output of the plot function for temporal correlation (default: stdout)\n");
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
  {"rotated",           no_argument, 0,  'r' },
  {"histfile",    required_argument, 0,  'q' },
  {"signal",      optional_argument, 0,  'p' },
  {"temp_corr_s", required_argument, 0,  '1' },
  {"temp_corr_f", required_argument, 0,  '2' },
  {NULL,                          0, 0,   0  }
};

#ifdef DEBUG
static void
interrupt(int sig)
{
  int i;

  printf("Current state:\n");
  for(i = 0; i < 2; ++i) {
    printf("Input %i:\n", i);
    printf("  Current file: %s\n", states_for_interrupt[i].input.filename);
    printf("  Current count: %"PRIu64"\n", states_for_interrupt[i].count_new);
    printf("  Last count: %"PRIu64"\n", states_for_interrupt[i].count_old);
    printf("  Current timestamp : %lf\n", states_for_interrupt[i].timestamp);
  }

  exit(sig);
}
#endif

int
main(int argc, char *argv[])
{
  int opt, ret, pos, i;
  FILE* tmp;
  char *out_filename = NULL;
  char *histo_filename = NULL;
  FILE *histo_file = NULL;
  FILE *histo_corr_file = NULL;
  FILE *signal_output = NULL;
  char *tmp_c;
  size_t uret;
  ssize_t sret;
  bool stats = false;
  bool print = false;

  struct state *states;
  struct first_run *first;
  struct statistics* statistics = NULL;
  struct second_run *second = NULL;

  pos = 0;
  interval = 0;
  secure_interval = 0;
  memset(u64_stats, 0, sizeof(uint64_t[2][2]));
  k = 0;
  compare_histo = NULL;
  histo_corr = NULL;

PRINTF("Debug enabled\n")

  states = calloc(SOURCES, sizeof(struct state));
  if (states == NULL) {
    printf("Malloc error\n");
    exit(-1);
  }
  first = calloc(SOURCES, sizeof(struct first_run));
  if (first == NULL) {
    printf("Malloc error\n");
    exit(-1);
  }

  while((opt = getopt_long(argc, argv, "ho:st:i:f:ark:q:p::", long_options, NULL)) != -1) {
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
        if (interval != 0) {
          printf("You can specify only one time slot duration\n");
          usage(-2, argv[0]);
        }
        ret = sscanf(optarg, "%lf", &interval);
        if (ret != 1) {
          printf("Bad time slot format\n");
          return -2;
        }
        break;
      case 'i':
        if (pos >= SOURCES) {
          printf("Too much input files (max : %i)\n", SOURCES);
          usage(-2, argv[0]);
        }
        tmp = fopen(optarg, "r");
        if (tmp == NULL || ferror(tmp)) {
          printf("Unable to load %s\n", optarg);
          return -1;
        }
        ret = zinit_read(&states[pos].input.input, tmp);
        if (ret != 0) {
          printf("Unable to initialize zlib : %i\n", ret);
          printf("(-5 == not a .gz input file)\n");
          return -1;
        }
        states[pos].input.filename = strdup(optarg);
        states[pos].input.filename_count_start = -1;
        ++pos;
        break;
      case 'f':
        if (pos < 1) {
          printf("-f option are not supposed to be before any -i option\n");
          usage(-2, argv[0]);
        }
        if (states[pos - 1].input.fixed_ip) {
          printf("Unable to have two different source addresses for the same flow\n");
          usage(-2, argv[0]);
        }
        assert(pos <= SOURCES);
        ret = inet_pton(AF_INET6, optarg, &states[pos - 1].input.src);
        assert(ret != -1);
        if (ret == 0) {
          printf("Invalid IPv6 address '%s'\n", optarg);
          return -2;
        }
        states[pos - 1].input.fixed_ip = true;
        break;
      case 'a':
        if (pos < 1) {
          printf("--origin option are not supposed to be before any -i option\n");
          usage(-2, argv[0]);
        }
        if (states[pos - 1].input.origin) {
          printf("Why did you put two --origin on the same -i ?\n");
          usage(-2, argv[0]);
        }
        assert(pos <= SOURCES);
        states[pos - 1].input.origin = true;
        break;
      case 'r':
        if (pos < 1) {
          printf("--rotated option are not supposed to be before any -i option\n");
          usage(-2, argv[0]);
        }
        if (states[pos - 1].input.filename_count_start >= 0) {
          printf("Why did you put two --rotated on the same -i ?\n");
          usage(-2, argv[0]);
        }
        assert(pos <= SOURCES);
        tmp_c = strrchr(states[pos - 1].input.filename, '.');
        if (tmp_c == NULL) {
          printf("Error in filename name\n");
          usage(-2, argv[0]);
        }
        *tmp_c = '\0';
        tmp_c = strrchr(states[pos - 1].input.filename, '.');
        if (tmp_c == NULL) {
          printf("Error in filename name: that's not a rotated file\n");
          usage(-2, argv[0]);
        }
        ++tmp_c;
        ret = sscanf(tmp_c, "%d", &states[pos - 1].input.filename_count_start);
        if (ret != 1) {
          printf("Error in filename name: that's not a rotated file (NaN)\n");
          usage(-2, argv[0]);
        }
        assert(states[pos - 1].input.filename_count_start >= 0);
        *(states[pos - 1].input.filename + strlen(states[pos - 1].input.filename)) = '.';
        states[pos - 1].input.filename_count = states[pos - 1].input.filename_count_start;
        break;
      case 'k':
        if (k != 0) {
          printf("-k option is not supposed to appear more than once\n");
          usage(-2, argv[0]);
        }
        ret = sscanf(optarg, "%i", &k);
        if (ret != 1) {
          printf("Error in -k option: Not a number !\n");
          usage(-2, argv[0]);
        }
        if (k < 0) {
          printf("Error, k needs to be >= 0\n");
          usage(-2, argv[0]);
        }
        if (k > 15) {
          printf("Error, k needs to be <= 15\n");
          usage(-2, argv[0]);
        }
        histo_mod = ((uint32_t) 1) << k;
        compare_histo = calloc(((size_t)1) << (2 * k), sizeof(uint64_t));
        if (compare_histo == NULL) {
          printf("Malloc error\n");
          exit(-1);
        }
        break;
      case 'q':
        if (histo_filename != NULL) {
          printf("-q option is not supposed to appear more than once\n");
          usage(-2, argv[0]);
        }
        histo_filename = optarg;
        break;
      case 'p':
        if (signal_output != NULL) {
          printf("-p option is not supposed to appear more than once\n");
          usage(-2, argv[0]);
        }
        if ((optarg != NULL) && (*optarg == '=')) {
          signal_output = fopen(optarg + 1, "w");
          if (signal_output  == NULL) {
            printf("Unable to open signal output file\n");
            return -1;
          }
        } else {
          signal_output = stdout;
        }
        break;
      case '1':
        if (histo_corr != NULL) {
          printf("--temp_corr_s option is not supposed to appear more than once\n");
          usage(-2, argv[0]);
        }
        ret = sscanf(optarg, "%zu", &long_history_size);
        if (ret != 1) {
          printf("Error in --temp_corr_s option: Not a number !\n");
          usage(-2, argv[0]);
        }
        long_history = calloc(long_history_size, sizeof(uint8_t));
        if (long_history == NULL) {
          printf("Malloc error\n");
          exit(-1);
        }
        long_history_current = long_history + long_history_size - 1;
        histo_corr = calloc(long_history_size, sizeof(struct historical_correlation));
        if (histo_corr == NULL) {
          printf("Malloc error\n");
          exit(-1);
        }
        long_history_looped = false;
        break;
      case '2':
        if (histo_corr_file != NULL) {
          printf("--temp_corr_f option is not supposed to appear more than once\n");
          usage(-2, argv[0]);
        }
        histo_corr_file = fopen(optarg, "w");
        if (histo_corr_file  == NULL) {
          printf("Unable to open signal output file\n");
          return -1;
        }
        break;
      default:
        usage(-1, argv[0]);
        break;
    }
  }

 if (argc > optind) {
    printf("Too much options (Be sure to use an \"=\" for optional arguments)\n");
    usage(-2, argv[0]);
    return 1;
  }

  if (pos < SOURCES) {
    printf("Not enough input files (%i < %i)\n", pos, SOURCES);
    usage(-2, argv[0]);
  }

  if (interval == 0) {
    printf("No time slot duration specified, unable to synchronyse inputs\n");
    usage(-2, argv[0]);
  }
  interval /= 1000;
  secure_interval = interval / 2;

  if (histo_filename != NULL) {
    if (k == 0) {
      printf("There is no default value for k, please specify the size wanted\n");
      usage(-2, argv[0]);
    }
    histo_file = fopen(histo_filename, "w");
    if (histo_file  == NULL) {
      printf("Unable to open histo output file\n");
      return -1;
    }
  } else {
    if (k != 0) {
      printf("Warning, -k option used without specifying file output, falling back to standard output\n");
    }
    histo_file = stdout;
  }

  if (out_filename != NULL) {
    output = fopen(out_filename, "w");
    if (output == NULL) {
      printf("Unable to open output file '%s'\n",out_filename);
      return -1;
    }
    print = true;
  } else if (!stats) {
    printf("No statistics required, no output file given: nothing to do, abording\n");
    usage(-2, argv[0]);
  }

  if (histo_corr_file == NULL) {
    if (histo_corr != NULL) {
      printf("Warning, --temp_corr_s option used without specifying file output (--temp_corr_f), falling back to standard output\n");
      histo_corr_file = stdout;
    }
  } else {
    if (histo_corr == NULL) {
      printf("Error, --temp_corr_f cannot be used without --temp_corr_s\n");
      usage(-2, argv[0]);
    }
  }

/*
  if () {
    second = calloc(SOURCES, sizeof(struct second_run));
    if (second == NULL) {
      printf("Malloc error\n");
      exit(-1);
    }
  }
*/

#ifdef DEBUG
  states_for_interrupt = states;
  signal(SIGINT, interrupt);
#endif

  synchronize_input(states);
  printf("Synchronisation obtained at count: %"PRIu64", %"PRIu64"\n", states[0].count_new, states[1].count_new);

  if (compare_histo != NULL) {
    for (i = 0; i < pos; ++i) {
      first[i].histo = histo_mod - 1;
    }
  }

  if (stats) {
    for (i = 0; i < pos; ++i) {
      first[i].bursts = calloc(1, sizeof(struct array_list_u64));
      if (first[i].bursts == NULL) {
        printf("Calloc error\n");
        exit(-1);
      }
    }
    coordbursts = calloc(1, sizeof(struct array_list_u64));
    if (coordbursts == NULL) {
      printf("Calloc error\n");
      exit(-1);
    }
  }

  do {
    sret = first_pass(output, print, first, states);
  } while (sret >= 0);

  if (states[0].input.input.input != NULL) {
    zread_end(&states[0].input.input);
  }
  if (states[1].input.input.input != NULL) {
    zread_end(&states[1].input.input);
  }
  print_desynchronisation_stats(states);
  printf("End at count: %"PRIu64", %"PRIu64"\n", states[0].count_new, states[1].count_new);
  if (sret < -1) {
    printf("Error before EOF : %i\n", sret);
  }

  if (out_filename != NULL) {
    fclose(output);
  }

  if (stats || second != NULL) {
    statistics = eval_stats(first);
  }

  if (stats) {
    print_stats(first, statistics);
  }

  if (k != 0) {
    print_histo(histo_file);
  }

  if (histo_filename != NULL) {
    fclose(histo_file);
  }

  if (signal_output != NULL) {
    print_signal_stats(signal_output, first);
    if (signal_output != stdout) {
      fclose(signal_output);
    }
  }

  if (histo_corr_file !=NULL) {
    print_histo_correlation(histo_corr_file, statistics);
    if (histo_corr_file != stdout) {
      fclose(histo_corr_file);
    }
  }

  if (stats) {
    for (i = 0; i < pos; ++i) {
      free(first[i].bursts);
    }
    free(coordbursts);
  }
  free(first);

  if (second != NULL) {
    memset(((uint8_t*)states) + sizeof(struct input_p), 0, sizeof(struct state) - sizeof(struct input_p));
    memset(((uint8_t*)(states + 1)) + sizeof(struct input_p), 0, sizeof(struct state) - sizeof(struct input_p));
    for (i = 0; i < pos; ++i) {
      if (states[i].input.filename_count_start >= 0) {
        states[i].input.filename_count = states[i].input.filename_count_start;
        uret = strlen(states[i].input.filename);
        assert(uret > 7);
        snprintf(states[i].input.filename + (uret - 6), 7, "%03i.gz", states[i].input.filename_count);
      }
      tmp = fopen(states[i].input.filename, "r");
      if (tmp == NULL || ferror(tmp)) {
        printf("Unable to reload %s for the second loop\n", states[i].input.filename);
        return -1;
      }
      ret = zinit_read(&states[i].input.input, tmp);
      if (ret != 0) {
        printf("Unable to re-initialize zlib : %i\n", ret);
        return -1;
      }
    }

    synchronize_input(states);
    do {
      sret = second_pass(output, print, second, states);
    } while (sret >= 0);

    if (states[0].input.input.input != NULL) {
      zread_end(&states[0].input.input);
    }
    if (states[1].input.input.input != NULL) {
      zread_end(&states[1].input.input);
    }
    if (sret < -1) {
      printf("Error before EOF (Second loop): %i\n", sret);
    }


    free(second);
  }

  free(states);

  return 0;
}
