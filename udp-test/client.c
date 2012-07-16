#include <assert.h>
#include <arpa/inet.h>
#include <ev.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/sockios.h>
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

/* udp buffers */
#define BUF_SIZE 1500
#define TIME_SIZE 128
#define CONTROL_SIZE 512
struct control {
  struct cmsghdr cm;
  char control[CONTROL_SIZE];
};

struct udp_io_t {
  int fd;
  int packet_len;
  struct sockaddr_in6 addr;
  uint64_t count;
  char buf[BUF_SIZE];
};

struct udp_io_t* buffer;

/* Event loop */
struct ev_loop      *event_loop;
struct ev_timer   *event_killer;
struct ev_periodic   *send_mess;

static void event_end(struct ev_loop *loop, struct ev_timer *w, int revents) {
  ev_unloop(event_loop, EVUNLOOP_ALL);
}


static void event_cb(struct ev_loop *loop, ev_periodic *periodic, int revents) {
  ssize_t len, sent_len;

  len = snprintf(buffer->buf, BUF_SIZE, ",%f,%"PRIu64"|", periodic->at, buffer->count);
  assert(len > 0);
  if (len <= buffer->packet_len) {
    len = buffer->packet_len;
  } else {
    --len;
  }
  sent_len = sendto(buffer->fd, buffer->buf, len, 0, (struct sockaddr *)&buffer->addr, sizeof(struct sockaddr_in6));
  if (sent_len == -1) {
    PERROR("sendto")
    return;
  }
  assert(sent_len == len);
  ++buffer->count;
}

static struct ev_periodic*
init(in_port_t port, struct in6_addr *addr, double offset, double delay, const uint64_t count, const int size, const char* interface, uint32_t scope)
{
  struct ev_periodic* event;


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
  buffer->count = count;
  buffer->packet_len = size;
  buffer->addr.sin6_family = AF_INET6;
  buffer->addr.sin6_port   = htons(port);
  memcpy(&buffer->addr.sin6_addr, addr, sizeof(struct in6_addr));

  /* Init event */
  event = (struct ev_periodic*) malloc(sizeof(struct ev_periodic));
  if (buffer == NULL) {
    PRINTF("Unable to use malloc\n")
    close(buffer->fd);
    free(buffer);
    return NULL;
  }
  ev_periodic_init(event, event_cb, offset, delay, NULL);
  ev_periodic_start(event_loop, event);
  return event;
}

static void down(int sig)
{
  ev_timer_set(event_killer, 0, 0);
  ev_timer_start(event_loop, event_killer);
}

/* Default Values */
#define DEFAULT_FILE stdout
#define DEFAULT_PORT 10101
#define DEFAULT_ADDRESS "::1"
#define DEFAULT_TIME_SECOND 0
#define DEFAULT_TIME_MILLISECOND 20
#define DEFAULT_COUNT 0
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
  printf(" -c, --count  <uint>  Specify the starting count of the outgoing packets (default: %i)\n", DEFAULT_COUNT);
  printf(" -l, --size   <size>  Specify the size of outgoing packets (default: %i)\n", DEFAULT_SIZE);
  printf(" -i, --bind   <name>  Specify the interface to bind one (default: no bind)\n");
  exit(err);
}

static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"dest",        required_argument, 0,  'd' },
  {"port",        required_argument, 0,  'p' },
  {"sec",         required_argument, 0,  's' },
  {"usec",        required_argument, 0,  'u' },
  {"count",       required_argument, 0,  'c' },
  {"size",        required_argument, 0,  'l' },
  {"bind",        required_argument, 0,  'i' },
  {NULL,                          0, 0,   0  }
};

int main(int argc, char *argv[]) {
  int opt;
  char *addr_s = NULL;
  char *interface = NULL;
  in_port_t port = DEFAULT_PORT;
  struct in6_addr addr = IN6ADDR_LOOPBACK_INIT;
  struct timeval delay;
  delay.tv_sec = DEFAULT_TIME_SECOND;
  delay.tv_usec = DEFAULT_TIME_MILLISECOND;
  uint64_t count = DEFAULT_COUNT;
  int size = DEFAULT_SIZE;
  uint32_t scope = 0;

  while((opt = getopt_long(argc, argv, "hd:p:s:u:c:l:i:", long_options, NULL)) != -1) {
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
      case 'c':
        if (count != DEFAULT_COUNT) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%"SCNu64, &count);
        break;
      case 'l':
        if (size != DEFAULT_SIZE) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%i", &size);
        break;
      case 'i':
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

  if (addr_s != NULL) {
    int tmp = inet_pton(AF_INET6, addr_s, &addr);
    if (tmp == 0) {
      printf("Format de destination invalide\n");
      return -3;
    }
    if (IN6_IS_ADDR_MULTICAST(&addr)) {
      if (!IN6_IS_ADDR_MC_LINKLOCAL(&addr)) {
        printf("Only link-scoped multicast addressess are accepted\n");
        return -3;
      }
      if (interface == NULL) {
        printf("An interface is needed for multicast\n");
        return -3;
      }
      scope = if_nametoindex(interface);
      if (scope == 0) {
        printf("Bad interface name\n");
        return -3;
      }
    }
  }

  event_loop = ev_default_loop (EVFLAG_AUTO);
  if((event_killer = (ev_timer*) malloc(sizeof(ev_timer))) == NULL) {
    PRINTF("Malloc\n")
    return -1;
  }
  ev_init(event_killer, event_end);

  /* Create buffer */
  buffer = (struct udp_io_t *)malloc(sizeof(struct udp_io_t));
  if (buffer == NULL) {
    PRINTF("Unable to use malloc\n")
    return -1;
  }
  memset(buffer, 0, sizeof(struct udp_io_t));


  send_mess = init(port, &addr, 0, delay.tv_sec + (((double) delay.tv_usec) / 1000), count, size, interface, scope);
  if (send_mess == NULL) {
    PRINTF("Unable to create sending event\n")
    return -2;
  }

  signal(SIGINT, down);
  ev_loop(event_loop, 0);

  free(send_mess);
  free(event_killer);

  return 0;
}
