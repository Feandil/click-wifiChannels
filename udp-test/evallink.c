#include <assert.h>
#include <arpa/inet.h>
#include <ev.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/sockios.h>
#include <ncurses.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "debug.h"
#include "monitor.h"

/** @file evallink.c Main system of the tool that tries to evaluate the links by broadcasting packets */

//! Size of the reception buffer
#define BUF_SIZE  1500
//! Size of short buffers for printing purposes
#define TMP_BUF     48
//! Number of lines per table
#define LINE_NB      4
//! Height of a text line
#define LINE_HEIGHT  1
//! Space left empty between two columns
#define COL_SEP      1
//! Height left emply between two lignes
#define LINE_SEP     1
//! Height left emply between two tables
#define PAR_SEP      3
//! Position of the first line (height)
#define FIRST_LINE   5
//! Length of the first column
#define FIR_COL_S   42
//! Length of the second column
#define SEC_COL_S    8
//! Length of the third column
#define THI_COL_S    6
//! Length of the fourth column
#define FOU_COL_S   24
//! Position of the first column
#define FIRST_COL    3
//! Position of the second column
#define SEC_COL      (FIRST_COL  + COL_SEP + FIR_COL_S)
//! Position of the third column
#define THIRD_COL    (SEC_COL    + COL_SEP + SEC_COL_S)
//! Position of the fourth column
#define FOURTH_COL   (THIRD_COL  + COL_SEP + THI_COL_S)
//! Position of the title (height)
#define TITLE_LINE   2
//! Position of the title (lateral)
#define TITLE_COL   22
//! Length of the title
#define TITLE_LEN   80

// Internal structure definition

/**
 * Description of a socket.
 */
struct eval_buffer {
  int fd;                   //!< The unix file descriptor.
  struct sockaddr_in6 addr; //!< The IPv6 address we are listenning on (manual bind()).
  char buf[BUF_SIZE];       //!< Buffer for the incomming packet.
};

/**
 * Description of a line of the output.
 */
struct output_line {
  WINDOW *ip;   //!< IP address of the considered node.
  WINDOW *db;   //!< Reception power in dBm.
  WINDOW *rate; //!< Reception rate in Mb/s.
  WINDOW *time; //!< Age of this piece of information.
};

/**
 * Internal description of a link.
 * This packet is also directly sent over the network.
 */
struct in_air {
  struct in6_addr  ip;   //!< IP of the given node.
  struct timespec stamp; //!< Timestamp of the reception of this information (in) or age of this piece of information (out).
  int8_t    db;          //!< Reception power in dBm.
  uint8_t rate;          //!< Reception rate in 0.5Mb/s.
};

/**
 * Description of a link.
 */
struct line {
  struct in_air data;        //!< Internal description.
  struct output_line output; //!< Output zones.
};


// Static variables

/**
 * Direct link information.
 * Contains data about how this node receives packets from other nodes.
*/
struct line inc[LINE_NB];

/**
 * Reverse link information.
 * Contains data about how other nodes receives packets from this node.
*/
struct line out[LINE_NB];

/**
 * Title of the page.
 */
WINDOW * title = NULL;

/**
 * Name of the monitoring interface.
 */
char   mon_name[IF_NAMESIZE];

/**
 * Static flags used to modify the behaviour of this script.
 * Possible values:
 * - EVALLINK_FLAG_DAEMON : no output (daemon).
 * - EVALLINK_FLAG_NOSEND : only listen on the link, do not send packets (slave).
 * - EVALLINK_FLAG_MON_EXIST : do not open a new monitoring interface (slace).
 */
char   static_flags;

/**
 * Static flag: no output (daemon).
 */
#define EVALLINK_FLAG_DAEMON  0x01

/**
 * Static flag: only listen on the link, do not send packets (slave).
 */
#define EVALLINK_FLAG_NOSEND  0x02

/**
 * Static flag: do not open a new monitoring interface (slace).
 */
#define EVALLINK_FLAG_MON_EXIST 0x04

// Libev related variables

/**
 * Event loop containing all the events.
 */
struct ev_loop      *event_loop;

/**
 * Special event used for graceful endings.
 * Unrolles all the other events, thus ends a "ev_loop(event_loop, 0);" call.
 */
struct ev_timer   *event_killer;

/**
 * Periodic event that sends the packets.
 */
struct ev_periodic   *send_mess;

/**
 * Event linked to a socket which receives packets from other nodes.
 */
struct ev_io         *recv_mess;

/**
 * Callback of the event_killer event.
 * Unrolles all the other events, thus ends a "ev_loop(event_loop, 0);" call.
 * Standard prototype for libev's ev_timer callback.
 * @param loop    Event loop which needs to be stopped.
 * @param w       Event that tiggered this call, should be 'event_killer'.
 * @param revents Causes of this call.
 */
static void
event_end(struct ev_loop *loop, struct ev_timer *w, int revents)
{
  ev_unloop(event_loop, EVUNLOOP_ALL);
}


// NCurses initialization/cleaning

/**
 * NCurses initialization.
 * Create the output zones.
 */
static void
ncurses_init(void)
{
  int pos;
  int y_pos;

  /* Init */
  initscr();
  /* Hide cursor */
  curs_set(0);

  /* Init windows */
  y_pos = FIRST_LINE;
  /* Init income windows */
  for (pos = 0; pos < LINE_NB; ++pos) {
    inc[pos].output.ip = newwin(LINE_HEIGHT, FIR_COL_S, y_pos, FIRST_COL);
    wrefresh(inc[pos].output.ip);
    inc[pos].output.db = newwin(LINE_HEIGHT, SEC_COL_S, y_pos, SEC_COL);
    wrefresh(inc[pos].output.db);
    inc[pos].output.rate = newwin(LINE_HEIGHT, THI_COL_S, y_pos, THIRD_COL);
    wrefresh(inc[pos].output.rate);
    inc[pos].output.time = newwin(LINE_HEIGHT, FOU_COL_S, y_pos, FOURTH_COL);
    wrefresh(inc[pos].output.time);
    y_pos += LINE_HEIGHT + LINE_SEP;
  }
  /* Paragraph change */
  y_pos += PAR_SEP;
  /* Init outgoing windows */
  for (pos = 0; pos < LINE_NB; ++pos) {
    out[pos].output.ip = newwin(LINE_HEIGHT, FIR_COL_S, y_pos, FIRST_COL);
    wrefresh(out[pos].output.ip);
    out[pos].output.db = newwin(LINE_HEIGHT, SEC_COL_S, y_pos, SEC_COL);
    wrefresh(out[pos].output.db);
    out[pos].output.rate = newwin(LINE_HEIGHT, THI_COL_S, y_pos, THIRD_COL);
    wrefresh(out[pos].output.rate);
    out[pos].output.time = newwin(LINE_HEIGHT, FOU_COL_S, y_pos, FOURTH_COL);
    wrefresh(out[pos].output.time);
    y_pos += LINE_HEIGHT + LINE_SEP;
  }
}

/**
 * NCurses cleaning.
 * Free all the windows created.
 */
static void
ncurses_stop(void)
{
  int pos;

  /* Free the title if it was created */
  if (title != NULL) {
    delwin(title);
  }

  /* Free in and out windows */
  for (pos = 0; pos < LINE_NB; ++pos) {
    delwin(inc[pos].output.ip);
    delwin(inc[pos].output.db);
    delwin(inc[pos].output.rate);
    delwin(inc[pos].output.time);
    delwin(out[pos].output.ip);
    delwin(out[pos].output.db);
    delwin(out[pos].output.rate);
    delwin(out[pos].output.time);
  }
  /* End ncurses */
  endwin();
}

static void update_time(void);

/**
 * Callback of the send_mess event.
 * Compute the age of informations concerning other nodes then broadcast it.
 * Standard prototype for libev's ev_periodic callback.
 * @param loop     Event loop on which this event is.
 * @param periodic Event that tiggered this call, should be 'send_mess'.
 * @param revents  Causes of this call.
 */
static void
send_cb(struct ev_loop *loop, ev_periodic *periodic, int revents)
{
  int ret, pos;
  size_t len;
  ssize_t sent_len;
  struct eval_buffer *buffer;
  struct timespec stamp;
  struct in_air   *output;

  /* Retreive the socket structure from the event */
  buffer = (struct eval_buffer*) periodic->data;
  assert(buffer != NULL);

  /* Retreive the current clock */
  ret = clock_gettime(CLOCK_MONOTONIC, &stamp);
  assert(ret == 0);

  /* For any node information, evaluate its age and add it to the outgoing buffer */
  len = 0;
  output = (struct in_air*) buffer->buf;
  for (pos = 0; pos < LINE_NB; ++pos) {
    if (*inc[pos].data.ip.s6_addr32 != 0) {
      memcpy(output, &inc[pos].data, sizeof(struct in_air));
      /* Remove a carry if we need to */
      if (stamp.tv_nsec > output->stamp.tv_nsec) {
        output->stamp.tv_nsec = stamp.tv_nsec - output->stamp.tv_nsec;
        output->stamp.tv_sec  = stamp.tv_sec - output->stamp.tv_sec;
      } else {
        output->stamp.tv_nsec = 1000000000 - output->stamp.tv_nsec + stamp.tv_nsec;
        assert(stamp.tv_sec >= output->stamp.tv_sec - 1);
        output->stamp.tv_sec  = stamp.tv_sec - output->stamp.tv_sec - 1;
      }
      len += sizeof(struct in_air);
      ++output;
    }
  }

  /* Add a zeroed in_air structure that indicates the end of the sent data */
  len += sizeof(struct in_air);
  memset(output, 0, sizeof(struct in_air));

  /* Send the packet */
  sent_len = sendto(buffer->fd, buffer->buf, len, 0, (struct sockaddr *)&buffer->addr, sizeof(struct sockaddr_in6));
  if (sent_len == -1) {
    PERROR("sendto")
    return;
  }
  assert(sent_len >= 0);
  assert(((size_t)sent_len) == len);

  /* Update our own output if we have one */
  if (!(static_flags & EVALLINK_FLAG_DAEMON)) {
    update_time();
  }
}

/**
 * Initializer of the send_mess periodic event.
 * @param port      Destination UDP port.
 * @param addr      Destibation IPv6 address (Should be multicast).
 * @param offset    Offset of the periodic event.
 * @param delay     Delay of the periodic event.
 * @param interface Interface to bind on (only if scope is 0).
 * @param scope "   Scope" of the IPv6 address if it's a link address (index of the corresponding interface).
 */
static struct ev_periodic*
send_on(in_port_t port, struct in6_addr *addr, double offset, double delay, const char* interface, uint32_t scope)
{
  struct ev_periodic* event;
  struct eval_buffer *buffer;

  /* Create buffer */
  buffer = calloc(1, sizeof(struct eval_buffer));
  if (buffer == NULL) {
    PRINTF("Unable to use malloc\n")
    return NULL;
  }

  /* Create socket */
  if ((buffer->fd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
    PERROR("socket")
    return NULL;
  }

  /* Bind to an interface if we were asked to */
  if (scope == 0) {
    if (interface != NULL) {
      if (setsockopt(buffer->fd, SOL_SOCKET, SO_BINDTODEVICE, interface, strlen(interface)) < 0) {
        printf("Unable to bind to device (You need to be root to do that ... do you really want to bind to this interface ?)\n");
        PERROR("setsockopt(SO_BINDTODEVICE)")
        return NULL;
      }
    }
  } else {
    buffer->addr.sin6_scope_id = scope;
  }

  /* Store the destination */
  buffer->addr.sin6_family = AF_INET6;
  buffer->addr.sin6_port   = htons(port);
  memcpy(&buffer->addr.sin6_addr, addr, sizeof(struct in6_addr));

  /* Init event */
  event = calloc(1, sizeof(struct ev_periodic));
  if (event == NULL) {
    PRINTF("Unable to use malloc\n")
    close(buffer->fd);
    free(buffer);
    return NULL;
  }
  ev_periodic_init(event, send_cb, offset, delay, NULL);
  event->data = buffer;
  ev_periodic_start(event_loop, event);
  return event;
}

/**
 * Clean and rewrite a NCurses window
 */
#define NCURSES_REWRITE_WINDOW_CONTENT(win, ...)  \
  werase(win);                                    \
  wmove(win, 0, 0);                               \
  wprintw(win, __VA_ARGS__);                      \
  wrefresh(win);

/**
 * Update the local output of a given table.
 * @param table Table to update.
 * @param tmp   Char array of size TMP_BUF used for caching.
 * @param stamp Timestamp offset if not NULL.
 */
inline static void
update_time_table(struct line table[], char* tmp, struct timespec *stamp)
{
  int pos, size;

  for (pos = 0; pos < LINE_NB; ++pos) {
    if (*table[pos].data.ip.s6_addr32 != 0) {
      if (stamp == NULL) {
        size = snprintf(tmp, TMP_BUF, "%ld.%09ld", table[pos].data.stamp.tv_sec, table[pos].data.stamp.tv_nsec);
      } else if (stamp->tv_nsec > table[pos].data.stamp.tv_nsec) {
        size = snprintf(tmp, TMP_BUF, "%ld.%09ld", stamp->tv_sec - table[pos].data.stamp.tv_sec, stamp->tv_nsec - table[pos].data.stamp.tv_nsec);
      } else {
        assert(stamp->tv_sec >= table[pos].data.stamp.tv_sec - 1);
        size = snprintf(tmp, TMP_BUF, "%ld.%09ld", stamp->tv_sec - table[pos].data.stamp.tv_sec - 1, 1000000000 - table[pos].data.stamp.tv_nsec + stamp->tv_nsec);
      }
      assert(size > 0);
      NCURSES_REWRITE_WINDOW_CONTENT(table[pos].output.time, "%.*s", size, tmp);
    }
  }
}


/**
 * Update both output tables.
 */
static void
update_time()
{
  char buf[TMP_BUF];
  int tmp;
  struct timespec stamp;

  /* Get the current clock */
  tmp = clock_gettime(CLOCK_MONOTONIC, &stamp);
  assert(tmp == 0);

  /* Update both tables */
  update_time_table(inc, buf, &stamp);
  update_time_table(out, buf, NULL);
}

/**
 * Data received callback.
 * Callback of a read_and_parse_monitor (from monitor.h) call, called by listen_cb.
 * @param stamp     Timestamp of the reception
 * @param rate      Rate at which the packet was received, in 0.5Mb/s.
 * @param signal    Signal at which the packet was received, in dBm.
 * @param from      IPv6 address of the sender.
 * @param data      Pointer to the memory zone containing the content of the packet.
 * @param len       Length of this memory zone.
 * @param machdr_fc Flags of the received frame (can contain for example the "Retry" flag).
 * @param arg       Pointer that was passed to the read_and_parse_monitor invocation, must contain a mon_io_t structure.
 */
static void
consume_data(struct timespec *stamp, uint8_t rate, int8_t signal, const struct in6_addr *from, \
             const char* data, size_t len, uint16_t machdr_fc, void* arg)
{
  char tmp[TMP_BUF];
  const char *ret;
  int size, pos, addr_pos;
  struct mon_io_t *mon;
  const struct in_air *incoming;

  /* Retreive the mon_io_t structure */
  mon = (struct mon_io_t*)arg;
  assert(mon != NULL);

  /* Discard any locally issued packets */
  for (addr_pos = 0; addr_pos < MAX_ADDR; ++addr_pos) {
    if (memcmp(from, &mon->ip_addr[addr_pos], sizeof(struct in6_addr)) == 0) {
      return;
    }
  }

  /* First store received values in inc */
  for (pos = 0; pos < LINE_NB; ++pos) {
    /* Check if we already know this IP (the sender) */
    if (*inc[pos].data.ip.s6_addr32 != 0) {
      if (memcmp(&inc[pos].data.ip, from, sizeof(struct in6_addr)) == 0) {
        inc[pos].data.rate = rate;
        inc[pos].data.db = signal;
        memcpy(&inc[pos].data.stamp, stamp, sizeof(struct timespec));
        if (!(static_flags & EVALLINK_FLAG_DAEMON)) {
          size = snprintf(tmp, TMP_BUF, "%"PRIi8"dBm", signal);
          assert(size > 0);
          NCURSES_REWRITE_WINDOW_CONTENT(inc[pos].output.db, "%s", tmp);
          size = snprintf(tmp, TMP_BUF, "%"PRIu8"%sMb/s", rate / 2, (rate % 2) ? ".5" : "");
          assert(size > 0);
          NCURSES_REWRITE_WINDOW_CONTENT(inc[pos].output.rate, "%s",tmp);
        }
        break;
      }
    } else {
      memcpy(&inc[pos].data.ip, from, sizeof(struct in6_addr));
      inc[pos].data.rate = rate;
      inc[pos].data.db = signal;
      memcpy(&inc[pos].data.stamp, stamp, sizeof(struct timespec));
      if (!(static_flags & EVALLINK_FLAG_DAEMON)) {
        ret = inet_ntop(AF_INET6, from, tmp, TMP_BUF);
        assert(ret != NULL);
        NCURSES_REWRITE_WINDOW_CONTENT(inc[pos].output.ip, "%s", tmp)
        size = snprintf(tmp, TMP_BUF, "%"PRIi8"dBm", signal);
        assert(size > 0);
        NCURSES_REWRITE_WINDOW_CONTENT(inc[pos].output.db, "%s", tmp);
        size = snprintf(tmp, TMP_BUF, "%"PRIu8"%sMb/s", rate / 2, (rate % 2) ? ".5" : "");
        assert(size > 0);
        NCURSES_REWRITE_WINDOW_CONTENT(inc[pos].output.rate, "%s", tmp);
      }
      break;
    }
  }

  if (!(static_flags & EVALLINK_FLAG_DAEMON)) {
    /* Then try to see if our ipaddress is inside thoses */
    for (addr_pos = 0; addr_pos < MAX_ADDR; ++addr_pos) {
      if (IN6_IS_ADDR_LINKLOCAL(&mon->ip_addr[addr_pos])) {
        /* If we still doesn't know our own real address, put it in the title now */
        if (title == NULL) {
          title = newwin(LINE_HEIGHT, TITLE_LEN, TITLE_LINE, TITLE_COL);
          ret = inet_ntop(AF_INET6, &mon->ip_addr[addr_pos], tmp, TMP_BUF);
          assert(ret != NULL);
          NCURSES_REWRITE_WINDOW_CONTENT(title, "%s", tmp)
          wrefresh(title);
        }
        incoming = (const struct in_air*) data;
        while (*incoming->ip.s6_addr32 != 0) {
          if (memcmp(&incoming->ip, &mon->ip_addr[addr_pos], sizeof(struct in6_addr)) == 0) {
            for (pos = 0; pos < LINE_NB; ++pos) {
              if (*out[pos].data.ip.s6_addr32 != 0) {
                if (memcmp(&inc[pos].data.ip, from, sizeof(struct in6_addr)) == 0) {
                  out[pos].data.rate = incoming->rate;
                  out[pos].data.db = incoming->db;
                  size = snprintf(tmp, TMP_BUF, "%"PRIi8"dBm", incoming->db);
                  assert(size > 0);
                  NCURSES_REWRITE_WINDOW_CONTENT(out[pos].output.db, "%s", tmp);
                  size = snprintf(tmp, TMP_BUF, "%"PRIu8"%sMb/s", incoming->rate / 2, (incoming->rate % 2) ? ".5" : "");
                  assert(size > 0);
                  NCURSES_REWRITE_WINDOW_CONTENT(out[pos].output.rate, "%s", tmp);
                  memcpy(&out[pos].data.stamp, &incoming->stamp, sizeof(struct timespec));
                  break;
                }
              } else {
                memcpy(&out[pos].data.ip, from, sizeof(struct in6_addr));
                ret = inet_ntop(AF_INET6, &out[pos].data.ip, tmp, TMP_BUF);
                assert(ret != NULL);
                NCURSES_REWRITE_WINDOW_CONTENT(out[pos].output.ip, "%s", tmp)
                out[pos].data.rate = incoming->rate;
                out[pos].data.db = incoming->db;
                size = snprintf(tmp, TMP_BUF, "%"PRIi8"dBm", incoming->db);
                assert(size > 0);
                NCURSES_REWRITE_WINDOW_CONTENT(out[pos].output.db, "%s", tmp);
                size = snprintf(tmp, TMP_BUF, "%"PRIu8"%sMb/s", incoming->rate / 2, (incoming->rate % 2) ? ".5" : "");
                assert(size > 0);
                NCURSES_REWRITE_WINDOW_CONTENT(out[pos].output.rate, "%s", tmp);
                memcpy(&out[pos].data.stamp, &incoming->stamp, sizeof(struct timespec));
                break;
              }
            }
          }
          ++incoming;
        }
      }
    }
    update_time();
  }
}

/**
 * Callback of the recv_mess event.
 * Use read_and_parse_monitor from monitor.h to analyse the packet
 * Standard prototype for libev's ev_io callback.
 * @param loop    Event loop on which this event is.
 * @param io      Event that tiggered this call, should be 'recv_mess'.
 * @param revents Causes of this call.
 */
static void
listen_cb(struct ev_loop *loop, ev_io *io, int revents)
{
  struct mon_io_t *mon;

  /* Retrieving data from event */
  mon = (struct mon_io_t*)io->data;
  assert(mon != NULL);

  /* Pass the data to the monitor.h parser */
  read_and_parse_monitor(mon, consume_data, mon);
}

/**
 * Create the libev io event that listen on the interface.
 * @param port          Port to bind on
 * @param mon_interface Name of the monitoring interface
 * @param phy_interface Index (WIPHY) of the underlying physical interface to monitor on
 * @param wan_interface Name of the underlying interface to monitor on
 * @param multicast     Multicast address to bind on
 * @return started libev io event which listen for new packets
 */
static struct ev_io*
listen_on(in_port_t port, const char* mon_interface, const uint32_t phy_interface, const char* wan_interface, \
          const struct in6_addr* multicast)
{
  struct ev_io* event;
  struct mon_io_t* mon;

  if (static_flags & EVALLINK_FLAG_MON_EXIST) {
    mon = monitor_listen_on(NULL, port, mon_interface, phy_interface, wan_interface, multicast, 0);
  } else {
    mon = monitor_listen_on(NULL, port, mon_interface, phy_interface, wan_interface, multicast, 1);
  }
  if (mon == NULL) {
    PRINTF("Unable to listen on monitoring interface")
    return NULL;
  }

  strncpy(mon_name, mon_interface, IF_NAMESIZE);

  /* Init event */
  event = calloc(1,sizeof(struct ev_io));
  if (event == NULL) {
    PRINTF("Unable to use malloc\n")
    return NULL;
  }
  ev_io_init(event, listen_cb, mon->fd, EV_READ);
  event->data = mon;
  ev_io_start(event_loop, event);
  return event;
}

/**
 * Callback for signals.
 * Use the event_killer event to stop the event loop.
 * @param sig Signal received.
 */
static void
down(int sig)
{
  ev_timer_set(event_killer, 0, 0);
  ev_timer_start(event_loop, event_killer);
}

/* Default Values */
//! Default port for evallink communications
#define EVALLINK_DEFAULT_PORT 10102
//! Default multicast address for evallink communications
#define EVALLINK_DEFAULT_ADDRESS "ff02::2"
//! Default interface for evallink communications
#define EVALLINK_DEFAULT_INTERFACE "wlan0"
//! Default interval in seconds between two evallink communications
#define EVALLINK_DEFAULT_TIME_SECOND 0
//! Default interval in milliseconds between two evallink communications
#define EVALLINK_DEFAULT_TIME_MILLISECOND 200
//! Default packet size for evallink communications
#define EVALLINK_DEFAULT_SIZE 900

/**
 * Print a short howto and exit.
 * @param err Execution code to return.
 * @param name Name under which this program was called.
 */
static void usage(int err, char *name)
{
  printf("%s: Send packets to the given destination\n", name);
  printf("Usage: %s [OPTIONS]\n", name);
  printf("Options:\n");
  printf(" -h, --help           Print this ...\n");
  printf(" -d, --daemon         Launch this program without any output (no ncurses)\n");
  printf(" -e, --slave          Do not send any packet, supposed to be used when a daemon is running\n");
  printf(" -a, --addr   <addr>  Specify the multicast address (default: %s)\n", EVALLINK_DEFAULT_ADDRESS);
  printf(" -p, --port   <port>  Specify the multisact port (default: %"PRIu16")\n", EVALLINK_DEFAULT_PORT);
  printf(" -s, --sec    <sec>   Specify the interval in second between two packets (default: %i)\n", EVALLINK_DEFAULT_TIME_SECOND);
  printf(" -m, --msec   <msec>  Specify the interval in millisecond between two packets (default: %i)\n", EVALLINK_DEFAULT_TIME_MILLISECOND);
  printf(" -l, --size   <size>  Specify the size of outgoing packets (default: %i)\n", EVALLINK_DEFAULT_SIZE);
  printf(" -i, --bind   <name>  Specify the interface to bind one (default: %s)\n", EVALLINK_DEFAULT_INTERFACE);
  exit(err);
}

/**
 * Long options used by getopt_long; see 'usage' for more detail.
 */
static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"daemon",            no_argument, 0,  'd' },
  {"slave",             no_argument, 0,  'e' },
  {"addr",        required_argument, 0,  'a' },
  {"port",        required_argument, 0,  'p' },
  {"sec",         required_argument, 0,  's' },
  {"usec",        required_argument, 0,  'u' },
  {"size",        required_argument, 0,  'l' },
  {"bind",        required_argument, 0,  'i' },
  {NULL,                          0, 0,   0  }
};

/**
 * Default address
 */
const char *default_address   = EVALLINK_DEFAULT_ADDRESS;

/**
 * Default interface
 */
const char *default_interface = EVALLINK_DEFAULT_INTERFACE;

/**
 * Main function for the evallink tool.
 * @param argc Argument Count
 * @param argv Argument Vector
 * @return Execution return code
 */
int
main(int argc, char *argv[])
{
  int opt;
  const char *addr_s = default_address;
  const char *interface = default_interface;
  in_port_t port = EVALLINK_DEFAULT_PORT;
  struct in6_addr addr = IN6ADDR_LOOPBACK_INIT;
  struct timeval delay;
  delay.tv_sec = EVALLINK_DEFAULT_TIME_SECOND;
  delay.tv_usec = EVALLINK_DEFAULT_TIME_MILLISECOND;
  int size = EVALLINK_DEFAULT_SIZE;
  uint32_t scope = 0;
  static_flags = 0;

  while((opt = getopt_long(argc, argv, "hdea:p:s:u:l:i:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage(0, argv[0]);
        return 0;
      case 'd':
        if (static_flags && EVALLINK_FLAG_NOSEND) {
          usage(1, argv[0]);
        }
        static_flags |= EVALLINK_FLAG_DAEMON;
        break;
      case 'e':
        if (static_flags && EVALLINK_FLAG_DAEMON) {
          usage(1, argv[0]);
        }
        static_flags |= EVALLINK_FLAG_NOSEND | EVALLINK_FLAG_MON_EXIST;
        break;
      case 'a':
        addr_s = optarg;
        break;
      case 'p':
        if (port != EVALLINK_DEFAULT_PORT) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%"SCNu16, &port);
        break;
      case 's':
        if (delay.tv_sec != EVALLINK_DEFAULT_TIME_SECOND) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%ld", &delay.tv_sec);
        break;
      case 'u':
        if (delay.tv_usec != EVALLINK_DEFAULT_TIME_MILLISECOND) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%li", &delay.tv_usec);
        break;
      case 'l':
        if (size != EVALLINK_DEFAULT_SIZE) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%i", &size);
        break;
      case 'i':
        if (interface != default_interface) {
          usage(1, argv[0]);
        }
        interface = optarg;
        break;
      default:
        usage(1, argv[0]);
        break;
    }
  }

 if(argc > optind) {
    usage(1, argv[0]);
    return 1;
  }

  int tmp = inet_pton(AF_INET6, addr_s, &addr);
  if (tmp == 0) {
    printf("Format de destination invalide\n");
    return -3;
  }
  if (!IN6_IS_ADDR_MC_LINKLOCAL(&addr)) {
    printf("Only link-scoped multicast addressess are accepted\n");
    return -3;
  }
  scope = if_nametoindex(interface);
  if (scope == 0) {
    if (interface == default_interface) {
      printf("Bad interface name\n");
      return -3;
    } else {
      printf("Bad interface name\n");
      return -3;
    }
  }

  memset(out, 0, sizeof(struct line) * LINE_NB);
  memset(inc, 0, sizeof(struct line) * LINE_NB);

  event_loop = ev_default_loop (EVFLAG_AUTO);
  if((event_killer = calloc(1, sizeof(ev_timer))) == NULL) {
    PRINTF("Malloc\n")
    return -1;
  }
  ev_init(event_killer, event_end);


  if (!(static_flags & EVALLINK_FLAG_NOSEND)) {
    send_mess = send_on(port, &addr, 0, delay.tv_sec + (((double) delay.tv_usec) / 1000), interface, scope);
    if (send_mess == NULL) {
      PRINTF("Unable to create sending event\n")
      return -2;
    }
  }

  recv_mess = listen_on(port, "mon0", 0, interface, &addr);
  if (recv_mess == NULL) {
    PRINTF("Unable to create receiving event\n")
    return -4;
  }

  signal(SIGINT, down);

  if (!(static_flags & EVALLINK_FLAG_DAEMON)) {
    ncurses_init();
  }

  ev_loop(event_loop, 0);

  if (!(static_flags & EVALLINK_FLAG_DAEMON)) {
    ncurses_stop();
  }
  if (!(static_flags & EVALLINK_FLAG_NOSEND)) {
    free(send_mess);
  }
  free(recv_mess);
  free(event_killer);

  if (!(static_flags & EVALLINK_FLAG_MON_EXIST)) {
    close_interface(mon_name);
  }

  return 0;
}
