#include <assert.h>
#include <arpa/inet.h>
#include <ev.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/net_tstamp.h>
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

#define BUF_SIZE  1500
#define TMP_BUF     48
#define LINE_NB      4
#define LINE_HEIGHT  1
#define COL_SEP      1
#define LINE_SEP     1
#define PAR_SEP      3
#define FIRST_LINE   3
#define FIR_COL_S   42
#define SEC_COL_S    8
#define THI_COL_S    6
#define FOU_COL_S   24
#define FIRST_COL    3
#define SEC_COL      (FIRST_COL  + COL_SEP + FIR_COL_S)
#define THIRD_COL    (SEC_COL    + COL_SEP + SEC_COL_S)
#define FOURTH_COL   (THIRD_COL  + COL_SEP + THI_COL_S)

struct udp_io_t {
  int fd;
  struct sockaddr_in6 addr;
  char buf[BUF_SIZE];
};

struct output_line {
  WINDOW *ip;
  WINDOW *db;
  WINDOW *rate;
  WINDOW *time;
};

struct in_air {
  struct in6_addr  ip;
  struct timespec stamp;
  int8_t    db;
  uint8_t rate;
};

struct line {
  struct output_line output;
  struct in_air data;
};

struct line inc[LINE_NB];
struct line out[LINE_NB];
char   mon_name[IFNAMSIZ];

/* Event loop */
struct ev_loop      *event_loop;
struct ev_timer   *event_killer;
struct ev_periodic   *send_mess;
struct ev_io         *recv_mess;

static void
event_end(struct ev_loop *loop, struct ev_timer *w, int revents)
{
  ev_unloop(event_loop, EVUNLOOP_ALL);
}


static void
ncurses_init()
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

static void
ncurses_stop()
{
  int pos;

  /* Free windows */
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

static void
send_cb(struct ev_loop *loop, ev_periodic *periodic, int revents)
{
  int pos;
  ssize_t len, sent_len;
  struct udp_io_t *buffer;
  struct timespec stamp;
  struct timespec tmp;
  struct in_air   *output;

  buffer = (struct udp_io_t*) periodic->data;
  assert(buffer != NULL);
  pos = clock_gettime(CLOCK_MONOTONIC, &stamp);
  assert(pos == 0);

  len = 0;
  output = (struct in_air*) buffer->buf;
  for (pos = 0; pos < LINE_NB; ++pos) {
    if (inc[pos].data.ip.s6_addr32 != 0) {
      memcpy(output, &inc[pos].data, sizeof(struct in_air));
      tmp.tv_nsec = output->stamp.tv_nsec;
      if (stamp.tv_nsec > tmp.tv_nsec) {
        output->stamp.tv_nsec = stamp.tv_nsec - tmp.tv_nsec;
        output->stamp.tv_sec  = stamp.tv_sec - output->stamp.tv_sec;
      } else {
        output->stamp.tv_nsec = 1000000000 - tmp.tv_nsec + stamp.tv_nsec;
        output->stamp.tv_sec  = stamp.tv_sec - output->stamp.tv_sec - 1;
      }
      len += sizeof(struct in_air);
      ++output;
    }
  }
  memset(output, 0, sizeof(struct in_air));
  len += sizeof(struct in_air);

  sent_len = sendto(buffer->fd, buffer->buf, len, 0, (struct sockaddr *)&buffer->addr, sizeof(struct sockaddr_in6));
  if (sent_len == -1) {
    PERROR("sendto")
    return;
  }
  assert(sent_len == len);
}

static struct ev_periodic*
send_on(in_port_t port, struct in6_addr *addr, double offset, double delay, const char* interface, uint32_t scope)
{
  struct ev_periodic* event;
  struct udp_io_t *buffer;

  /* Create buffer */
  buffer = (struct udp_io_t *)malloc(sizeof(struct udp_io_t));
  if (buffer == NULL) {
    PRINTF("Unable to use malloc\n")
    return NULL;
  }
  memset(buffer, 0, sizeof(struct udp_io_t));

  /* Create socket */
  if ((buffer->fd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
    PERROR("socket")
    return NULL;
  }

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
  buffer->addr.sin6_family = AF_INET6;
  buffer->addr.sin6_port   = htons(port);
  memcpy(&buffer->addr.sin6_addr, addr, sizeof(struct in6_addr));

  /* Init event */
  event = (struct ev_periodic*) malloc(sizeof(struct ev_periodic));
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

static void inline
update_time_table(struct line table[], char* tmp, struct timespec *stamp)
{
  int pos, size;

  for (pos = 0; pos < LINE_NB; ++pos) {
    if (inc[pos].data.ip.s6_addr32 != 0) {
      if (stamp->tv_nsec == 0 && stamp->tv_sec == 0) {
        size = snprintf(tmp, TMP_BUF, "%ld.%09ld", table[pos].data.stamp.tv_sec, table[pos].data.stamp.tv_nsec);
      } else if (stamp->tv_nsec > table[pos].data.stamp.tv_nsec) {
        size = snprintf(tmp, TMP_BUF, "%ld.%09ld", stamp->tv_sec - table[pos].data.stamp.tv_sec, stamp->tv_nsec - table[pos].data.stamp.tv_nsec);
      } else {
        size = snprintf(tmp, TMP_BUF, "%ld.%09ld", stamp->tv_sec - table[pos].data.stamp.tv_sec - 1, 1000000000 - table[pos].data.stamp.tv_nsec + stamp->tv_nsec);
      }
      assert(size > 0);
      wprintw(table[pos].output.db, "%.*s", size, tmp);
    }
  }
}

static void
update_time()
{
  char buf[TMP_BUF];
  int tmp;

  struct timespec stamp;

  tmp = clock_gettime(CLOCK_MONOTONIC, &stamp);
  assert(tmp == 0);
  update_time_table(inc, buf, &stamp);
  stamp.tv_nsec = 0;
  stamp.tv_sec = 0;
  update_time_table(out, buf, &stamp);
}

static void
consume_data(struct timespec *stamp, uint8_t rate, int8_t signal, const struct in6_addr *from, \
             const char* data, ssize_t len, uint16_t machdr_fc, void* arg)
{
  char tmp[TMP_BUF];
  int size, pos, addr_pos;
  struct mon_io_t *mon;
  struct in_air *incoming;

  mon = (struct mon_io_t*)arg;
  assert(mon != NULL);

  /* First store values in inc */
  for (pos = 0; pos < LINE_NB; ++pos) {
    if (inc[pos].data.ip.s6_addr32 != 0) {
      if (memcmp(&inc[pos].data.ip, from, sizeof(struct in6_addr*)) == 0) {
        inc[pos].data.rate = rate;
        inc[pos].data.db = signal;
        memcpy(&inc[pos].data.stamp, stamp, sizeof(struct timespec*));
        size = snprintf(tmp, TMP_BUF, "%"PRIi8"dBm", signal);
        assert(size > 0);
        wprintw(inc[pos].output.db, "%.*s", size, tmp);
        wrefresh(out[pos].output.db);
        size = snprintf(tmp, TMP_BUF, "%"PRIu8"%sMb/s", rate / 2, (rate % 2) ? ".5" : "");
        assert(size > 0);
        wprintw(inc[pos].output.rate, "%.*s", size, tmp);
        wrefresh(out[pos].output.rate);
        break;
      }
    } else {
      memcpy(&inc[pos].data.ip, from, sizeof(struct in6_addr*));
      inc[pos].data.rate = rate;
      inc[pos].data.db = signal;
      memcpy(&inc[pos].data.stamp, stamp, sizeof(struct timespec*));
      size = snprintf(tmp, TMP_BUF, "%"PRIi8"dBm", signal);
      assert(size > 0);
      wprintw(inc[pos].output.db, "%.*s", size, tmp);
      wrefresh(out[pos].output.db);
      size = snprintf(tmp, TMP_BUF, "%"PRIu8"%sMb/s", rate / 2, (rate % 2) ? ".5" : "");
      assert(size > 0);
      wprintw(inc[pos].output.rate, "%.*s", size, tmp);
      wrefresh(out[pos].output.rate);
      break;
    }
  }

  /* Then try to see if our ipaddress is inside thoses */
  for (addr_pos = 0; addr_pos < MAX_ADDR; ++addr_pos) {
    if (IN6_IS_ADDR_LINKLOCAL(&mon->ip_addr[addr_pos])) {
      incoming = (struct in_air*) data;
      while (incoming->ip.s6_addr32 != 0) {
        if (memcmp(&incoming->ip, &mon->ip_addr[addr_pos], sizeof(struct in6_addr*)) == 0) {
          for (pos = 0; pos < LINE_NB; ++pos) {
            if (inc[pos].data.ip.s6_addr32 != 0) {
              if (memcmp(&inc[pos].data.ip, from, sizeof(struct in6_addr*)) == 0) {
                inc[pos].data.rate = incoming->rate;
                inc[pos].data.db = incoming->db;
                memcpy(&inc[pos].data.stamp, &incoming->stamp, sizeof(struct timespec*));
                break;
              }
            } else {
              memcpy(&inc[pos].data.ip, from, sizeof(struct in6_addr*));
              inc[pos].data.rate = incoming->rate;
              inc[pos].data.db = incoming->db;
              memcpy(&inc[pos].data.stamp, &incoming->stamp, sizeof(struct timespec*));
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

static void
listen_cb(struct ev_loop *loop, ev_io *io, int revents)
{
  struct mon_io_t *mon;

  mon = (struct mon_io_t*)io->data;
  assert(mon != NULL);
  read_and_parse_monitor(mon, consume_data, mon);
}

static struct ev_io*
listen_on(in_port_t port, const char* mon_interface, const int phy_interface, const char* wan_interface, \
          const struct in6_addr* multicast)
{
  struct ev_io* event;
  struct mon_io_t* mon;

  mon = monitor_listen_on(NULL, port, mon_interface, phy_interface, wan_interface, multicast, 1);
  if (mon == NULL) {
    PRINTF("Unable to listen on monitoring interface")
    return NULL;
  }

  strncpy(mon_name, mon_interface, IFNAMSIZ);

  /* Init event */
  event = (struct ev_io*) malloc(sizeof(struct ev_io));
  if (event == NULL) {
    PRINTF("Unable to use malloc\n")
    return NULL;
  }
  ev_io_init(event, listen_cb, mon->fd, EV_READ);
  event->data = mon;
  ev_io_start(event_loop, event);
  return event;
}

static void down(int sig)
{
  ev_timer_set(event_killer, 0, 0);
  ev_timer_start(event_loop, event_killer);
}

/* Default Values */
#define DEFAULT_PORT 10102
#define DEFAULT_ADDRESS "ff02::2"
#define DEFAULT_INTERFACE "wlan0"
#define DEFAULT_TIME_SECOND 0
#define DEFAULT_TIME_MILLISECOND 200
#define DEFAULT_SIZE 900

static void usage(int err, char *name)
{
  printf("%s: Send packets to the given destination\n", name);
  printf("Usage: %s [OPTIONS]\n", name);
  printf("Options:\n");
  printf(" -h, --help           Print this ...\n");
  printf(" -d, --dest   <addr>  Specify the destination address (default: %s)\n", DEFAULT_ADDRESS);
  printf(" -p, --port   <port>  Specify the destination port (default: %"PRIu16")\n", DEFAULT_PORT);
  printf(" -s, --sec    <sec>   Specify the interval in second between two packets (default: %i)\n", DEFAULT_TIME_SECOND);
  printf(" -m, --msec   <msec>  Specify the interval in millisecond between two packets (default: %i)\n", DEFAULT_TIME_MILLISECOND);
  printf(" -l, --size   <size>  Specify the size of outgoing packets (default: %i)\n", DEFAULT_SIZE);
  printf(" -i, --bind   <name>  Specify the interface to bind one (default: %s)\n", DEFAULT_INTERFACE);
  exit(err);
}

static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"dest",        required_argument, 0,  'd' },
  {"port",        required_argument, 0,  'p' },
  {"sec",         required_argument, 0,  's' },
  {"usec",        required_argument, 0,  'u' },
  {"size",        required_argument, 0,  'l' },
  {"bind",        required_argument, 0,  'i' },
  {NULL,                          0, 0,   0  }
};

const char *default_address   = DEFAULT_ADDRESS;
const char *default_interface = DEFAULT_INTERFACE;

int main(int argc, char *argv[]) {
  int opt;
  const char *addr_s = default_address;
  const char *interface = default_interface;
  in_port_t port = DEFAULT_PORT;
  struct in6_addr addr = IN6ADDR_LOOPBACK_INIT;
  struct timeval delay;
  delay.tv_sec = DEFAULT_TIME_SECOND;
  delay.tv_usec = DEFAULT_TIME_MILLISECOND;
  int size = DEFAULT_SIZE;
  uint32_t scope = 0;

  while((opt = getopt_long(argc, argv, "hd:p:s:u:l:i:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage(0, argv[0]);
        return 0;
      case 'd':
        addr_s = optarg;
        break;
      case 'p':
        if (port != DEFAULT_PORT) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%"SCNu16, &port);
        break;
      case 's':
        if (delay.tv_sec != DEFAULT_TIME_SECOND) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%ld", &delay.tv_sec);
        break;
      case 'u':
        if (delay.tv_usec != DEFAULT_TIME_MILLISECOND) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%li", &delay.tv_usec);
        break;
      case 'l':
        if (size != DEFAULT_SIZE) {
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

  event_loop = ev_default_loop (EVFLAG_AUTO);
  if((event_killer = (ev_timer*) malloc(sizeof(ev_timer))) == NULL) {
    PRINTF("Malloc\n")
    return -1;
  }
  ev_init(event_killer, event_end);


  send_mess = send_on(port, &addr, 0, delay.tv_sec + (((double) delay.tv_usec) / 1000), interface, scope);
  if (send_mess == NULL) {
    PRINTF("Unable to create sending event\n")
    return -2;
  }

  recv_mess = listen_on(port, "mon0", 0, interface, &addr);
  if (recv_mess == NULL) {
    PRINTF("Unable to create receiving event\n")
    return -4;
  }

  signal(SIGINT, down);
  ncurses_init();

  ev_loop(event_loop, 0);

  ncurses_stop();
  free(send_mess);
  free(recv_mess);
  free(event_killer);
  close_interface(mon_name);

  return 0;
}