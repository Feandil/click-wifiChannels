#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <getopt.h>
#include <gsl/gsl_cdf.h>
#include <limits.h>
#include <math.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "debug.h"
#include "zutil.h"

/** @file extract.c Main system of the tool that extract statisticts from outputs of our server */


#ifdef DEBUG
# include <signal.h>
#endif

/* Stupid dynamic structure */
/**
 * Number of element per list step.
 */
#define LIST_STEP 7
/**
 * "Array list" : chained list of arrays
 */
struct array_list_u64 {
  uint64_t   data[LIST_STEP];  //!< Data contain in the list
  struct array_list_u64 *next; //!< Next list element (NULL at the end)
};

/**
 * Increment an indexed counter in an "Array List" (Recursive function).
 * @param list  "Array List" countaining the indexed counters
 * @param count Index of the counter to increment
 */
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

/**
 * Decrease an indexed counter in an "Array List" (Recursive function).
 * The corresponding counter NEED to be accessible and > 0 (asserts)
 * @param list  "Array List" countaining the indexed counters
 * @param count Index of the counter to increment
 */
static void
decrease_counter(struct array_list_u64 *list, uint64_t count)
{
  assert(list != NULL);
  if (count >= LIST_STEP) {
    assert(list->next != NULL);
    decrease_counter(list->next, count - LIST_STEP);
  } else {
    assert(list->data[count] != 0);
    list->data[count] -= 1;
  }
}

/**
 * Input description.
 */
struct input_p
{
  char               *filename;             //!< Filename of the currently opened file
  int                 filename_count_start; //!< Index of the first rotated file, -1 if non rotated files
  int                 filename_count;       //!< Index of the current rotated file, undefined if non rotated files
  struct zutil_read   input;                //!< Zutil opaque structure used to decompress the input
  struct in6_addr     src;                  //!< If not zeroed, IPv6 address of the source to be considered (if the data contain more than one source IP)
  bool                fixed_ip;             //!< Indicates if 'src' is empty or not
  bool                origin;               //!< If true, use the origin timestamp to synchronyse paquets. If false, use the received timestamp
};

/**
 * Window size for forward resynchronisation
 */
#define DELAY_BEFORE_RESYNCHRONISATION  15000

/**
 * Input state description
 */
struct state
{
  struct input_p  input;                           //!< Input description
  uint64_t        count_new;                       //!< Counter of the last received packet
  uint64_t        count_old;                       //!< Counter of the previous received packet
  double          timestamp;                       //!< Timestamp of the last received packet
  double          timestamp_old;                   //!< Timestamp of the previous received packet
  int8_t          signal_new;                      //!< Signal strength of the last received packet
  int8_t          signal_old;                      //!< Signal strength of the previous received packet
  uint64_t        desynchronization_drop_internal; //!< Number of packet dropped because of an internal desynchronization (Packet too late compared to its estimated arrive time)
  uint64_t        desynchronization_drop_external; //!< Number of packet dropped because of an externaldesynchronization (Packet too late compared to other streams' estimated arrive time)
  uint64_t        desynchronization_resync;        //!< Number of global resynchronisation
  uint64_t        resynchronisation_counter;       //!< Number of packets that were successively late just before the current packet
  double          resynchronisation_min;           //!< Minimum lateness of packets that were successively late just before the current packet
};

/**
 * States stored during a first run
 */
struct first_run
{
  uint32_t       histo;                   //!< Past 'k' events (receive/loose packet)
  long double signal_m;                   //!< Sum of the signal strength of received packet
  long double signal_a;                   //!< Sum of the signal strength of packet received just after a loss
  long double signal_b;                   //!< Sum of the signal strength of packet received just before a loss
  long double signal_e;                   //!< Sum of the signal strength of packets of the other stream during a loss
  uint64_t  signal_m_c;                   //!< Number of elements in 'signal_m'
  uint64_t signal_ab_c;                   //!< Number of elements in 'signal_a' and 'signal_b'
  uint64_t  signal_e_c;                   //!< Number of elements in 'signal_e'
  uint64_t signal_strengh[UINT8_MAX + 1]; //!< Occurrences of given signal strength
  struct array_list_u64 *bursts;          //!< "Array List" of burst lengths
};

/**
 * Common and basic statics extracted from the first run
  */
struct statistics {
  uint64_t partial_i[2]; //!< Probabilty of success/loss over the first channel, independently of the second one
  uint64_t partial_j[2]; //!< Probabilty of success/loss over the second channel, independently of the first one
  uint64_t total;        //!< Total number of event seen
};

/**
 * States stored during a second run
 */
struct second_run
{
  void *null;  //!< Nothing
};

#ifdef DEBUG
/**
 * For debuging purpose on interrupts (ctrl-c), a static pointer to the current states in stored
 */
struct state *states_for_interrupt;
#endif

/* Global cache */
//! Number of source files
#define SOURCES 2

/**
 * Difference of synchronization between the two stream.
 * For two synchronized packets, we have:
 * sync_count_diff = (int64_t) (states[0].count_new - states[1].count_new)
 */
int64_t sync_count_diff;
//! Theoretical interval between two packets
double   interval;
//! Half of the theoretical interval between two packets
double   secure_interval;
//! Counter for all the possible states
uint64_t u64_stats[2][2];
//! Historically-dependent states counters
uint64_t *compare_histo;
//! "Array List" of coordinated burst length
struct array_list_u64 *coordbursts;
//! Number of events to be kept for 'compare_histo'
int k;
//! ((uint32_t) 1) << k
uint32_t histo_mod;
//! Complete map of signals strength occurences
uint64_t signals[UINT8_MAX + 1][UINT8_MAX + 1];
//! Structure storing dependency between states
struct historical_correlation {
  uint64_t data[4][3]; //!< Counter of the event j known i (data[i][j])
};
//! Size of the correlation history stored
size_t long_history_size;
//! Circular buffer containing the long_history_size last events
uint8_t *long_history;
//! Starting point of the long_history circular buffer
uint8_t *long_history_current;
//! History auto-correlation
struct historical_correlation *histo_corr;
//! Initialization: only true if the buffer long_history is full
bool long_history_looped;
//! Floating mean length
size_t floating_mean_length;
//! Floating mean file
FILE *floating_mean_output;

/**
 * Filter "ssize_t" length into acceptable "int" length
 */
#if __WORDSIZE == 64
# define PRINTF_PRECISION(a) \
  ((a > INT_MAX) ? INT_MAX : \
  ((a < 0) ? 0 : (int)a))
#else /* __WORDSIZE == 64 */
# define PRINTF_PRECISION(a) \
  (a < 0 ? 0 : a)
#endif /* __WORDSIZE == 64 */

/**
 * Read an input line, try to extract the contained data and update the input state description.
 * Filter source IPv6,
 * Filter packet too late compared to their espected arrive time,
 * Filter backward resynchronization
 * Exit in case of format error
 * @param in_state Input state description to be updated.
 * @return EOF: -1, nothing (packet dropped): 0, new information without sync: 1, new information with backward sync: 2, new information with forward sync: 3
 */
static ssize_t
read_input(struct state *in_state)
{
  ssize_t len;
  char *buffer, *next;
  struct in6_addr ip;
  int tmp;
  double temp_ts, temp_diff;

  /* Read a new line */
  buffer = zread_line(&in_state->input.input, &len);
  if (buffer == NULL) {
    if (len < -1) {
      goto exit;
    }
    return -1;
  }
  assert(len >= 0);

  /* Verify the IP address */
  /* Extract the IPv6 in string format */
  next = memchr(buffer, ',', (size_t)len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the IP address field : ''%.*s'')\n", PRINTF_PRECISION(len), buffer);
    goto exit;
  }
  *next = '\0';
  /* Transform into binary representation */
  tmp = inet_pton(AF_INET6, buffer, &ip);
  assert(tmp != -1);
  if (tmp == 0) {
    printf("Invalid IPv6 address '%s'\n", buffer);
    goto exit;
  }
  /* Try to match the IP */
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
  /* Next entry */
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Skip the flag field */
  assert(len >= 0);
  next = memchr(buffer, ',', (size_t)len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the flag field : ''%.*s'')\n", PRINTF_PRECISION(len), buffer);
    goto exit;
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Store the signal strengh */
  /* Extract the signal in string format */
  assert(len >= 0);
  next = memchr(buffer, ',', (size_t)len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the signal field : ''%.*s'')\n", PRINTF_PRECISION(len), buffer);
    goto exit;
  }
  /* Save previous value */
  in_state->signal_old = in_state->signal_new;
  /* Transform into binary representation directly in the destination memory */
  *next = '\0';
  tmp = sscanf(buffer, "%"SCNd8, &in_state->signal_new);
  if (tmp != 1) {
    printf("Bad input format (count isn't a int8: ''%.*s'')\n", PRINTF_PRECISION(len), buffer);
    goto exit;
  }
  /* Next entry */
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Skip the channel field */
  assert(len >= 0);
  next = memchr(buffer, ',', (size_t)len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the rate field : ''%.*s'')\n", PRINTF_PRECISION(len), buffer);
    goto exit;
  }
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Look at the origin timestamp field */
  /* Extract the origin timestamp in string format */
  assert(len >= 0);
  next = memchr(buffer, ',', (size_t)len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the origin timestamp field : ''%.*s'')\n", PRINTF_PRECISION(len), buffer);
    goto exit;
  }
  if (in_state->input.origin) {
    /* If we are looking at the origin timestamp, transform into binary representation directly in the destination memory */
    *next = '\0';
    tmp = sscanf(buffer, "%lf", &in_state->timestamp);
    if (tmp != 1) {
      printf("Bad input format (origin timestamp isn't a double: ''%.*s'')\n", PRINTF_PRECISION(len), buffer);
      goto exit;
    }
  }
  /* Next entry */
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Read the count field */
  /* Extract the counter in string format */
  assert(len >= 0);
  next = memchr(buffer, ',', (size_t)len);
  if (next == NULL) {
    printf("Bad input format (no closing ',' for the sent timestamp field : ''%.*s'')\n", PRINTF_PRECISION(len), buffer);
    goto exit;
  }
  /* Save previous value */
  in_state->count_old = in_state->count_new;
    /* Transform into binary representation directly in the destination memory */
  *next = '\0';
  tmp = sscanf(buffer, "%"SCNd64, &in_state->count_new);
  if (tmp != 1) {
    printf("Bad input format (count isn't a uint64: ''%.*s'')\n", PRINTF_PRECISION(len), buffer);
    goto exit;
  }
  /* Verify that the count is strictly increasing */
  if ((in_state->count_new <= in_state->count_old) && (in_state->count_new != 0)) {
    printf("Bad input format (count isn't strictly increasing %"PRIu64" after %"PRIu64")\n", in_state->count_new, in_state->count_old);
    goto exit;
  }
  /* Next entry */
  ++next;
  len -= (next - buffer);
  buffer = next;

  /* Only the reception timestamp should remain */
  if (!in_state->input.origin) {
    /* If we are not looking at the origin timestamp, transform into binary representation directly in the destination memory */
    tmp = sscanf(buffer, "%lf", &in_state->timestamp);
    if (tmp != 1) {
      printf("Bad input format (reception timestamp isn't a double: ''%.*s'')\n", PRINTF_PRECISION(len), buffer);
      goto exit;
    }
  }

  /* Compare to the estimated arrive time */
  if (in_state->timestamp_old != 0) {
    /* Evaluate estimated arrive time based the last one, the theoretical interval and the counter increase */
    temp_ts = in_state->timestamp_old + interval * (double)(in_state->count_new - in_state->count_old);
    if (temp_ts - in_state->timestamp > secure_interval) {
       /* We are correcting this kind of drift at each step, thus this should not happen */
       printf("Algorithmic error: a packet arrived too early (%lf VS %lf : %lf VS %lf)\n", in_state->timestamp, temp_ts, temp_ts - in_state->timestamp, secure_interval);
       goto exit;
    } else if (in_state->timestamp - temp_ts > interval) {
      /* The packet arrived more than a window after its due time, it has no value, drop it */
      PRINTF("File %s, ", in_state->input.filename);
      PRINTF("packet %"PRIu64" dropped because outside its window (%lf VS %lf)\n", in_state->count_new, in_state->timestamp, temp_ts)
      in_state->signal_new = in_state->signal_old;
      in_state->count_new = in_state->count_old;
      ++in_state->desynchronization_drop_internal;
      return 0;
    } else {
      if (in_state->timestamp <= temp_ts) {
        /* The packet arrived before we were expecting it, we are late, resynchronization */
        PRINTF("File %s, ", in_state->input.filename);
        PRINTF("resynchronisation @%"PRIu64": %f\n", in_state->count_new,  in_state->timestamp - temp_ts)
        in_state->timestamp_old = in_state->timestamp;
        in_state->resynchronisation_counter = 0;
        in_state->resynchronisation_min = interval;
        return 2;
      } else {
        /* The packet was late, update the "successively late" states */
        ++in_state->resynchronisation_counter;
        temp_diff = in_state->timestamp - temp_ts;
        if (temp_diff < in_state->resynchronisation_min) {
          in_state->resynchronisation_min = temp_diff;
        }
        /* If our window is full, update the estimation */
        if (in_state->resynchronisation_counter > DELAY_BEFORE_RESYNCHRONISATION) {
          PRINTF("File %s, ", in_state->input.filename);
          PRINTF("resynchronisation @%"PRIu64": +%f\n", in_state->count_new,  in_state->resynchronisation_min);
          in_state->timestamp_old = temp_ts + in_state->resynchronisation_min;
          in_state->resynchronisation_counter = 0;
          in_state->resynchronisation_min = interval;
          return 3;
        } else {
          in_state->timestamp_old = temp_ts;
        }
      }
    }
  }

  return 1;
exit:
  printf("Error parsing input file %s (last count : %"PRIu64")\n", in_state->input.filename, in_state->count_new);
  exit(-3);
}

/**
 * Read the input until a valid packet is found.
 * Resynchronize the streams in case of a global desynchronization
 * @param inc    Input state description to be updated.
 * @param states Table containing the two different streams
 * @return EOF: -1, ok: 1-3
 */
static ssize_t
next_input(struct state *inc, struct state *states)
{
  ssize_t tmp;
  double other_ts;

  do {
    tmp = read_input(inc);
    if ((tmp > 1) && (states != NULL)) {
      if (inc == states) {
        other_ts = states[1].timestamp_old + interval * (double) (((int64_t) (states[0].count_new - states[1].count_new)) - sync_count_diff);
      } else {
        assert(inc == states + 1);
        other_ts = states[0].timestamp_old + interval * (double) (((int64_t) (states[1].count_new - states[0].count_new)) + sync_count_diff);
      }
      switch(tmp) {
        case 2:
          /* Time went back */
          if (inc->timestamp_old < other_ts - 1.01 * secure_interval) {
            assert(inc->timestamp_old + interval < other_ts + 1.01 * secure_interval);
            if (inc == states) {
              ++sync_count_diff;
            } else {
              --sync_count_diff;
            }
            /* Make one packet disappear */
            PRINTF("Resynchronisation: %s was too early, removing a packet", inc->input.filename)
            if (inc->count_old == inc->count_new - 1) {
              inc->signal_new = inc->signal_old;
              ++inc->desynchronization_resync;
              tmp = 0;
              PRINTF(" that triggered a new call\n")
            } else {
              ++inc->count_old;
              PRINTF("\n")
            }
          }
          break;
        case 3:
          /* Time went forward */
          if (inc->timestamp_old > other_ts + 1.01 * secure_interval) {
            assert(inc->timestamp_old - interval > other_ts - 1.01 * secure_interval);
            if (inc == states) {
              --sync_count_diff;
              ++inc;
            } else {
              ++sync_count_diff;
              --inc;
            }
            /* Make one packet disappear */
            PRINTF("Resynchronisation: %s was too late, removing a packet of the other stream", inc->input.filename)
            if (inc->count_old == inc->count_new - 1) {
              inc->signal_new = inc->signal_old;
              ++inc->desynchronization_resync;
              tmp = 0;
              PRINTF(" that triggered a new call\n")
            } else {
              ++inc->count_old;
              PRINTF("\n")
            }
          }
          break;
      }
    }
  } while (tmp == 0);
  return tmp;
}

/**
 * Synchronize the two counters.
 * Stops as soon as two packets are in the same window
 * @param states Inputs
 */
static void
synchronize_input(struct state* states)
{
  ssize_t tmp;
  double   ts;

#if SOURCES != 2
# error "This function was written only for 2 sources"
#endif

  /* Initialization: read one line from both input*/
  tmp = next_input(states, NULL);
  if (tmp < 0) {
    printf("End of file before any input for input file 0 (0-1) ...\n");
    exit(-4);
  }
  tmp = next_input(states + 1, NULL);
  if (tmp < 0) {
    printf("End of file before any input for input file 1 (0-1)...\n");
    exit(-4);
  }
  /* Read packets in the stream with the older timestamp until synchronization */
  while (1) {
    ts = states[0].timestamp - states[1].timestamp;
    if (ts > secure_interval) {
      tmp = next_input(states + 1, NULL);
      if (tmp < 0) {
        printf("End of file before synchronisation for input file 1 (0-1)...\n");
        exit(-4);
      }
    } else if (ts < -secure_interval) {
      tmp = next_input(states, NULL);
      if (tmp < 0) {
        printf("End of file before synchronisation for input file 0 (0-1)...\n");
        exit(-4);
      }
    } else {
      break;
    }
  }
  /* Set the synchronization related states */
  sync_count_diff = (int64_t) (states[0].count_new - states[1].count_new);
  states[0].count_old = states[0].count_new;
  states[0].timestamp_old = states[0].timestamp;
  states[1].count_old = states[1].count_new;
  states[1].timestamp_old = states[1].timestamp;
}

/**
 * Read the input until a valid packet is found, handles file changes in case of rotated files.
 * @param in_state Input state description to be updated.
 * @param states   Table containing the two different streams
 * @return EOF: -1, ok: 1, Zlib error : < 0
 */
static int
next_line_or_file(struct state *in_state, struct state *states)
{
  ssize_t tmp;
  size_t len;
  int ret;
  FILE *src;

  /* Try to read directly */
  tmp = next_input(in_state, states);
  if (tmp != -1) {
#if __WORDSIZE == 64
    if (tmp > INT_MAX) {
      return INT_MAX;
    } else if (INT_MIN) {
      return INT_MIN;
    } else {
      return (int)tmp;
    }
#else /* __WORDSIZE == 64 */
    return tmp;
#endif /* __WORDSIZE == 64 */
  }

  /* If we are not using rotated files : EOF */
  if (in_state->input.filename_count_start < 0) {
    return -1;
  }
  ++in_state->input.filename_count;
  if (in_state->input.filename_count > 1000) {
    return -1;
  }

  /* Update the filename */
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

  /* Close and reopen zlib encoding stream */
  zread_end(&in_state->input.input);
  memset(&in_state->input.input, 0, sizeof(struct zutil_read));
  ret = zinit_read(&in_state->input.input, src);
  if (ret < 0) {
    printf("Zlib encoding error or no dat\n");
    return ret;
  }
  PRINTF("Success\n");
  /* Use recursive call to find a valid packet */
  return next_line_or_file(in_state, states);
}

/**
 * Transform one stream into 0's and 1's
 */
static void
simple_print(struct state *inc, FILE* out)
{
  ssize_t tmp;
  uint64_t i;

  /* Initialization: read one line */
  tmp = next_input(inc, NULL);
  if (tmp < 0) {
    printf("End of file before any input ...\n");
    exit(-4);
  }
  if (interval != 0) {
    inc->timestamp_old = inc->timestamp;
  }
  inc->count_old = inc->count_new;
  while (next_line_or_file(inc, NULL) >= 0) {
    for (i = inc->count_old; i < inc->count_new - 1; ++i) {
      fprintf(out, "0");
    }
    fprintf(out, "1");
  }
}

/**
 * Update the history autocorrelation.
 * @param new_state State express in binary :
 *  0b00 means 0 0
 *  0b10 means 1 0
 *  0b01 means 0 1
 *  0b11 means 1 1
 */
inline static void
temporal_dependence(uint8_t new_state)
{
  uint8_t *current, *end;
  uint64_t errors[2];
  size_t mean_temp_len;
  struct historical_correlation *histo_current;

  /* Only try to store if we already have a full buffer and it's not an unisteresting input */
  if (histo_corr != NULL && long_history_looped && (new_state != 0b11)) {
    histo_current = histo_corr;

    /* First part: long_history_current + 1 -> long_history + long_history_size */
    end = long_history + long_history_size;
    current = long_history_current + 1;

    assert(long_history <= current);
    assert(current <= end);

    while (current < end) {
      ++histo_current->data[*current][new_state];
      ++histo_current;
      ++current;
    }

    /* Second part: long_history -> long_history_current + 1 */
    current = long_history;
    end = long_history_current + 1;

    while (current < end) {
      ++histo_current->data[*current][new_state];
      ++histo_current;
      ++current;
    }
  }

  /* Add data to the inverse floating mean */
  if (floating_mean_output != NULL && long_history_looped) {
    errors[0] = 0;
    errors[1] = 0;
    mean_temp_len = floating_mean_length;

    /* First part: long_history_current + 1 -> long_history + long_history_size */
    end = long_history + long_history_size;
    current = long_history_current + 1;

    assert(long_history <= current);
    assert(current <= end);

    while (current < end && mean_temp_len > 0) {
      errors[0] += !((*current) & 0b10);
      errors[1] += !((*current) & 0b01);
      ++current;
      --mean_temp_len;
    }

    /* Second part: long_history -> long_history_current + 1 */
    current = long_history;
    end = long_history_current + 1;

    while (current < end && mean_temp_len > 0) {
      errors[0] += !((*current) & 0b10);
      errors[1] += !((*current) & 0b01);
      ++current;
      --mean_temp_len;
    }
    fprintf(floating_mean_output, "%Lf %Lf\n", ((long double)1) - ((long double)errors[0]) / ((long double)floating_mean_length), \
                                               ((long double)1) - ((long double)errors[1]) / ((long double)floating_mean_length)); //"
  }

  /* Store state and update current position */
  *long_history_current = new_state;
  --long_history_current;
  if (long_history_current < long_history) {
    long_history_looped = true;
    long_history_current = long_history + long_history_size - 1;
  }
}

/**
 * Add a state in two part (i,j) in the first run
 */
#define ADD_VAL(a,b)                                          \
  if (compare_histo != NULL) {                                \
    data[0].histo = ((data[0].histo << 1) + a) % histo_mod;   \
    data[1].histo = ((data[1].histo << 1) + b) % histo_mod;   \
    compare_histo[(data[0].histo << k) + data[1].histo] += 1; \
  }                                                           \
  ++u64_stats[a][b];

/**
 * Add a state in one part 0bij in the first run
 */
#define ADD_VAR_ONE(a)         \
  if (long_history != NULL) {  \
    temporal_dependence(a);    \
  }

/**
 * Increase the burst counting
 */
#define ADD_BURST(dest,size)                 \
  if (dest != NULL) {                        \
    ({                                       \
      uint64_t count_ = size - 1;            \
      if (count_ > 0) {                      \
        increment_counter(dest, count_ - 1); \
      }                                      \
    });                                      \
  }

/**
 * Decrease the burst counting
 */
#define REMOVE_BURST(dest,size)              \
  if (dest != NULL) {                        \
    ({                                       \
      uint64_t count_ = size - 1;            \
      if (count_ > 0) {                      \
        decrease_counter(dest, count_ - 1);  \
      }                                      \
    });                                      \
  }

/**
 * Go back in time (make one packet disappear
 */
#define UNREAD_LINE(pos)                                                        \
  REMOVE_BURST(data[pos].bursts, states[pos].count_new - states[pos].count_old) \
  if (states[pos].count_new - states[pos].count_old > 1) {                      \
    data[pos].signal_b -= states[pos].signal_old;                               \
    data[pos].signal_a -= states[pos].signal_new;                               \
    --data[pos].signal_ab_c;                                                    \
  }                                                                             \
  data[pos].signal_strengh[(uint8_t)states[pos].signal_new] -= 1;


/**
 * Read a line, update associated states
 */
#define READ_LINE(pos)                                                       \
  tmp = next_line_or_file(states + pos, states);                             \
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

/**
 * Read on step the available input and update the stats of the first run.
 * @param out    Output file for translation into 0's and 1's
 * @param data   First run related states
 * @param states Input description
 * @return OK: >= 0, EOF: -1, ERROR: < -1
 */
static ssize_t
first_pass(FILE* out, struct first_run *data, struct state *states)
{
  ssize_t tmp;
  uint64_t age[2];
  uint64_t i;
  double   ts;

  /* Calculate the ages */
  assert(states[0].count_new >= states[0].count_old);
  age[0] = states[0].count_new - states[0].count_old;
  assert(states[1].count_new >= states[1].count_old);
  age[1] = states[1].count_new - states[1].count_old;

  assert(((int64_t)(states[0].count_old - states[1].count_old)) == sync_count_diff);
  if (age[0] == age[1]) {
    /* We have synchrone packets, verify that they are really synchrone */
    ts = states[0].timestamp - states[1].timestamp;
    if (fabs(ts) > interval) {
      /* Not good: outside the windows */
      if (states[0].timestamp_old - states[1].timestamp > secure_interval || states[1].timestamp - states[0].timestamp_old > interval) {
        /* External desynchro : 1 was too late compared to the expected date of 0 */
        ++states[1].desynchronization_drop_external;
        PRINTF("File %s, ", states[1].input.filename);
        PRINTF("packet %"PRIu64" dropped because outside the global window (%lf (ref %lf) VS ref %lf)\n", states[1].count_new, states[1].timestamp, states[1].timestamp_old, states[0].timestamp_old);
        UNREAD_LINE(1)
        states[1].timestamp_old -= interval * (double)(states[1].count_new - states[1].count_old);
        states[1].signal_new = states[1].signal_old;
        states[1].count_new = states[1].count_old;
        READ_LINE(1)
        return tmp;
      } else if (states[1].timestamp_old - states[0].timestamp > secure_interval || states[0].timestamp - states[1].timestamp_old > interval) {
        /* External desynchro : 0 was too late compared to the expected date of 1 */
        ++states[0].desynchronization_drop_external;
        PRINTF("File %s, ", states[0].input.filename);
        PRINTF("packet %"PRIu64" dropped because outside the global window (%lf (ref %lf) VS ref %lf)\n", states[0].count_new, states[0].timestamp, states[0].timestamp_old, states[1].timestamp_old);
        UNREAD_LINE(0)
        states[0].timestamp_old -= interval * (double)(states[0].count_new - states[0].count_old);
        states[0].signal_new = states[0].signal_old;
        states[0].count_new = states[0].count_old;
        READ_LINE(0)
        return tmp;
      } else {
        /* Real big desynchronization that wasn't corrected by the algorithm */
        printf("Desynchronisation between %"PRIu64" and %"PRIu64"\n", states[0].count_new, states[1].count_new);
        printf("current: %lf - %lf -> %lf (VS %lf)\n", states[0].timestamp, states[1].timestamp, fabs(ts), interval);
        printf("ref:     %lf - %lf  \n", states[0].timestamp_old, states[1].timestamp_old);
        printf("(%"PRIu64" and %"PRIu64")\n",states[0].count_old, states[1].count_old);
        exit(4);
      }
    }
    if (age[0] != 0) {
      for (i = age[0]; i > 1; --i) {
        if (out != NULL) {
          fprintf(out, "0 0\n");
        }
        ADD_VAL(0,0)
        ADD_VAR_ONE(0b00)
        ++signals[INT8_MAX + 1][INT8_MAX + 1];
      }
      ADD_BURST(coordbursts, age[0])
      if (out != NULL) {
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
      if (out != NULL) {
        fprintf(out, "0 0\n");
      }
      ADD_VAL(0,0)
      ADD_VAR_ONE(0b00)
      ++signals[INT8_MAX + 1][INT8_MAX + 1];
    }
    ADD_BURST(coordbursts, age[0])
    if (out != NULL) {
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
      if (out != NULL) {
        fprintf(out, "0 0\n");
      }
      ADD_VAL(0,0)
      ADD_VAR_ONE(0b00)
      ++signals[INT8_MAX + 1][INT8_MAX + 1];
    }
    ADD_BURST(coordbursts, age[1])
    if (out != NULL) {
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

/**
 * Read on step the available input and update the stats of the first run.
 * @param data   Second run related states
 * @param states Input description
 * @return OK: >= 0, EOF: -1, ERROR: < -1
 */
static ssize_t
second_pass(struct second_run *data, struct state *states)
{
  ssize_t tmp;
  uint64_t age[2];
  uint64_t i;
  double   ts;

  assert(states[0].count_new >= states[0].count_old);
  age[0] = states[0].count_new - states[0].count_old;
  assert(states[1].count_new >= states[1].count_old);
  age[1] = states[1].count_new - states[1].count_old;

  assert(((int64_t)(states[0].count_old - states[1].count_old)) == sync_count_diff);
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
    tmp = next_line_or_file(states, states);
    if (tmp < 0) {
      return tmp;
    }
    tmp = next_line_or_file(states + 1, states);
    return tmp;
  } else if (age[0] < age[1]) {
    for(i = age[0]; i > 1; --i) {

    }

    states[1].count_old += age[0];
    tmp = next_line_or_file(states, states);
    return tmp;
  } else /* age[0] > age[1] */ {
    for(i = age[1]; i > 1; --i) {

    }

    states[0].count_old += age[1];
    tmp = next_line_or_file(states + 1, states);
    return tmp;
  }
}

/**
 * Helper function for evaluating the Likelihood Ratio Statistic for independence test
 * @param nij n_{i,j}
 * @param ni  sum_{j=0}^{1} n_{i,j}
 * @param nj  sum_{i=0}^{1} n_{i,j}
 * @param n   sum_{j=0}^{1} sum_{i=0}^{1} n_{i,j}
 * @return   nij * log ((n * nij)/(ni * nj))
 */
static long double
lrs_part(uint64_t nij, uint64_t ni, uint64_t nj, uint64_t n)
{
  return ((long double) nij) * logl((((long double) n) * ((long double) nij)) / (((long double) ni) * ((long double) nj)));
}

/**
 * Helper function for evaluating the Pearson Chi-squared Statistic for independence test
 * @param nij n_{i,j}
 * @param ni  sum_{j=0}^{1} n_{i,j}
 * @param nj  sum_{i=0}^{1} n_{i,j}
 * @param n   sum_{j=0}^{1} sum_{i=0}^{1} n_{i,j}
 * @return  (nij - (ni * nj / n))² / (ni * nj / n)
 */
static long double
pcs_part(uint64_t nij, uint64_t ni, uint64_t nj, uint64_t n)
{
  long double temp = (((long double) ni) * ((long double) nj)) / ((long double) n);
  long double square = ((long double) nij) - temp;

  return square * square / temp;
}

/**
 * Evaluate global and simple statistics
 * @param data Direct statistics from the first run
 * @return Evaluated simple stats
 */
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

/**
 * Display on stdout desynchronization information.
 * @param states Direct statistics from a run
 */
static void
print_desynchronisation_stats(struct state *states)
{
  printf("Desynchronisation drops :\n");
  printf(" Internals: %"PRIu64" and %"PRIu64"\n", states[0].desynchronization_drop_internal, states[1].desynchronization_drop_internal);
  printf(" Externals: %"PRIu64" and %"PRIu64"\n", states[0].desynchronization_drop_external, states[1].desynchronization_drop_external);
  printf("Resynchronisations:  %"PRIu64" and %"PRIu64"\n", states[0].desynchronization_resync, states[1].desynchronization_resync);
}

/**
 * Diplay more advanced statistics.
 * @param data Direct statistics from the first run
 * @param stats Evaluated simple stats
 */
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

/**
 * Print signal related statistics
 * @param signal_output FILE used for the output
 * @param data Direct statistics from the first run
 */
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

/**
 * Helper macro for print_histo: remplace the value with -1 if too big
 */
#define LIMIT_MAX_VAL(x) ({ uint64_t val_ = x; (val_ > max) ? (-1) : ((int64_t)val_); })

/**
 * Helper macro for print_histo: Evaluate distribution output if the distribution were independant
 */
#define INDEP(i,j)  ((uint64_t)(((long double)(independant[i] * independant[j + histo_mod])) / total))

/**
 * Helper macro for print_histo: Difference between the independant distribution and the measured one
 */
#define INDEP_DIFF(i,j)  ((int64_t)(compare_histo[((i) * histo_mod) + j] - INDEP(i,j)))

/**
 * Helper macro for print_histo: Emulate a 'signed log': log(1 + x * signe(x))
 */
#define SIGNED_LOG(a) ({ int64_t a_ = a; a_ >= 0 ? logl(1 + (long double)a_) : -logl(-(long double)a_); })

/**
 * Print historical autocorrelation statistics
 * @param histo_output FILE used for the output
 */
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

  free(independant);
}
#undef LIMIT_MAX_VAL
#undef SIGNED_LOG
#undef INDEP_DIFF
#undef INDEP

/**
 * Print_histo_correlation helper: Print list separator
 */
#define PRINT_SEP                 \
  fprintf(histo_corr_file, ", ");

/**
 * Print_histo_correlation helper: Print axis "distribution"
 */
#define PRINT_AXIS                                          \
  fprintf(histo_corr_file, "[1:1:%zu]", long_history_size);

/**
 * Print_histo_correlation helper: Start a named plot in a new figure
 */
#define PRINT_PLOT_START(a,b)                             \
  fprintf(histo_corr_file, "%% %s:\n", a);                \
  fprintf(histo_corr_file, "figure('Name','%s');\n", b);  \
  fprintf(histo_corr_file, "plot(");

/**
 * Print_histo_correlation helper: Add a constant reference line to a plot
 */
#define PRINT_REFERENCE(a,b)                   \
  temp = a;                                    \
  PRINT_AXIS                                   \
  fprintf(histo_corr_file, ", [");             \
  for (i = 0; i < long_history_size - 1; ++i)  \
    fprintf(histo_corr_file, "%Lf ", temp);    \
  fprintf(histo_corr_file, "%Lf]", temp);      \
  fprintf(histo_corr_file, ", '%s'", b);

/**
 * Print_histo_correlation helper: Print direct data
 */
#define PRINT_CURVE(a,b,c,d)                                                                            \
  PRINT_AXIS                                                                                            \
  fprintf(histo_corr_file, ", [");                                                                      \
  for (i = 0; i < long_history_size - 1; ++i)                                                           \
    fprintf(histo_corr_file, "%Lf ", histo_corr[i].data[a][b] / ((long double) c));                     \
  fprintf(histo_corr_file, "%Lf]", histo_corr[long_history_size - 1].data[a][b] / ((long double) c));   \
  fprintf(histo_corr_file, ", '%s'", d);

/**
 * Print_histo_correlation helper: Print summaries
 */
#define PRINT_CURVE_DOUBLE_SUM(a,b,c,d)                                                                                                                                                              \
  PRINT_AXIS                                                                                                                                                                                         \
  fprintf(histo_corr_file, ", [");                                                                                                                                                                   \
  for (i = 0; i < long_history_size - 1; ++i)                                                                                                                                                        \
    fprintf(histo_corr_file, "%Lf ", (histo_corr[i].data[a][b] + histo_corr[i].data[a][0b00] + histo_corr[i].data[0b00][b] + histo_corr[i].data[0b00][0b00]) / ((long double) c + u64_stats[0][0])); \
  i = long_history_size - 1;                                                                                                                                                                         \
  fprintf(histo_corr_file, "%Lf]", (histo_corr[i].data[a][b] + histo_corr[i].data[a][0b00] + histo_corr[i].data[0b00][b] + histo_corr[i].data[0b00][0b00]) / ((long double) c + u64_stats[0][0]));   \
  fprintf(histo_corr_file, ", '%s'", d);

/**
 * Print_histo_correlation helper: Close a plot, add legend.
 */
#define PRINT_PLOT_END(a)                       \
  fprintf(histo_corr_file, ");\n");             \
  fprintf(histo_corr_file, "legend(%s);\n", a); \
  fprintf(histo_corr_file, "\n");

/**
 * Diplay more advanced statistics.
 * @param histo_corr_file FILE used for the output
 * @param stats Evaluated simple stats
 */
static void
print_histo_correlation(FILE *histo_corr_file, struct statistics* stats)
{
  size_t i;
  long double temp;

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

}

#undef PRINT_PLOT_END
#undef PRINT_CURVE_DOUBLE_SUM
#undef PRINT_CURVE
#undef PRINT_REFERENCE
#undef PRINT_PLOT_START
#undef PRINT_AXIS
#undef PRINT_SEP

/**
 * Print a short howto and exit.
 * @param error Execution code to return.
 * @param name  Name under which this program was called.
 */
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
  printf(" --mean_length <len>  Length of the floating interval for the floating mean (default: 0, desactivated). If temp_corr_s is used, need to be smaller than temp_corr_s\n");
  printf(" --mean_file   <f>    File for the output of the floating mean (default: stdout)\n");
  exit(error);
}

/**
 * Long options used by getopt_long; see 'usage' for more detail.
 */
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
  {"mean_length", required_argument, 0,  '3' },
  {"mean_file",   required_argument, 0,  '4' },
  {NULL,                          0, 0,   0  }
};

#ifdef DEBUG
/**
 * Callback in case of captured interrupt.
 * Print current state information
 */
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

/**
 * Main function for the extract tool.
 * @param argc Argument Count
 * @param argv Argument Vector
 * @return Execution return code
 */
int
main(int argc, char *argv[])
{
  int opt, ret, pos, i;
  FILE* tmp;
  FILE* output;
  char *out_filename = NULL;
  char *histo_filename = NULL;
  FILE *histo_file = NULL;
  FILE *histo_corr_file = NULL;
  FILE *signal_output = NULL;
  char *tmp_c;
  size_t uret;
  ssize_t sret;
  bool stats = false;

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
  floating_mean_length = 0;
  floating_mean_output = NULL;

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
      case '3':
        if (floating_mean_length != 0) {
          printf("-k option is not supposed to appear more than once\n");
          usage(-2, argv[0]);
        }
        ret = sscanf(optarg, "%zu", &floating_mean_length);
        if (ret != 1) {
          printf("Error in --mean_length option: Not a number !\n");
          usage(-2, argv[0]);
        }
        break;
      case '4':
        if (floating_mean_output != NULL) {
          printf("--mean_file option is not supposed to appear more than once\n");
          usage(-2, argv[0]);
        }
        floating_mean_output = fopen(optarg, "w");
        if (floating_mean_output  == NULL) {
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

  interval /= 1000;
  secure_interval = interval / 2;

  if (pos < SOURCES) {
    if (pos == 1) {
      if (out_filename != NULL) {
        output = fopen(out_filename, "w");
        if (output == NULL) {
          printf("Unable to open output file '%s'\n", out_filename);
          return -1;
        }
        simple_print(states,output);
        fclose(output);
        free(states);
        free(first);
        return 0;
      } else {
        printf("Only one input file without output file : error\n");
        usage(-2, argv[0]);
      }
    }
    printf("Not enough input files (%i < %i)\n", pos, SOURCES);
    usage(-2, argv[0]);
  }

  if (interval == 0) {
    printf("No time slot duration specified, unable to synchronyse inputs\n");
    usage(-2, argv[0]);
  }

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
  } else {
    output = NULL;
    if (!stats) {
      printf("No statistics required, no output file given: nothing to do, abording\n");
      usage(-2, argv[0]);
    }
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

  if (floating_mean_length != 0) {
    if (floating_mean_output == NULL) {
      printf("Warning, --mean_length option used without specifying file output (--mean_file), falling back to standard output\n");
      floating_mean_output = stdout;
    }
    if (histo_corr != NULL) {
      if (long_history_size < floating_mean_length) {
        printf("When temp_corr_s is used with the floating mean, it needs to be defined bigger than the interval mean length\n");
        usage(-2, argv[0]);
      }
    } else {
      long_history_size = floating_mean_length;
      long_history = calloc(long_history_size, sizeof(uint8_t));
      if (long_history == NULL) {
        printf("Malloc error\n");
        exit(-1);
      }
      long_history_current = long_history + long_history_size - 1;
      long_history_looped = false;
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
    sret = first_pass(output, first, states);
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
    printf("Error before EOF : %zi\n", sret);
  }

  if (out_filename != NULL) {
    fclose(output);
  }

  statistics = eval_stats(first);

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

  if (histo_corr_file != NULL) {
exit(1);
    print_histo_correlation(histo_corr_file, statistics);
    if (histo_corr_file != stdout) {
      fclose(histo_corr_file);
    }
  }

  if (floating_mean_output != NULL && floating_mean_output != stdout) {
    fclose(floating_mean_output);
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
      sret = second_pass(second, states);
    } while (sret >= 0);

    if (states[0].input.input.input != NULL) {
      zread_end(&states[0].input.input);
    }
    if (states[1].input.input.input != NULL) {
      zread_end(&states[1].input.input);
    }
    if (sret < -1) {
      printf("Error before EOF (Second loop): %zi\n", sret);
    }


    free(second);
  }

  free(statistics);
  free(states);

  return 0;
}
