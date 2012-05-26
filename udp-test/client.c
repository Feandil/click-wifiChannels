#include <assert.h>
#include <arpa/inet.h>
#include <event.h>
#include <getopt.h>
#include <inttypes.h>
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
#include "debug.h"

/* udp buffers */
#define BUF_SIZE 1500
struct udp_io_t {
  int fd;
  struct event* event;
  int len;
  int packet_len;
  struct timeval delay;
  uint64_t count;
  struct sockaddr_in6 addr;
  char buf[BUF_SIZE];
};

/* Event loop */
struct event_base* gbase;
struct event*      glisten;

inline static void set_data(struct udp_io_t* data) {
  ssize_t len;
  struct timespec date;
  int i = clock_gettime(CLOCK_MONOTONIC, &date);
  assert(i == 0);
  len = snprintf(data->buf, BUF_SIZE, ",%lu.%li,%"PRIu64"|", date.tv_sec, date.tv_nsec, data->count);
  PRINTF("%"PRIu64" sent\n", data->count)
  assert(len > 0);
  data->len = len;
  ++data->count;
}

static void event_cb(int fd, short event, void *arg) {
  ssize_t len;
  struct udp_io_t* in;

  assert(arg != NULL);
  in = (struct udp_io_t*) arg;

  set_data(in);
  if (in->len <= in->packet_len) {
    in->len = in->packet_len;
  } else {
    --in->len;
  }
  len = sendto(in->fd, in->buf, in->len, 0, (struct sockaddr *)&in->addr, sizeof(struct sockaddr_in6));
  if (len == -1) {
    PERROR("sendto")
    return;
  }
  assert(len == in->len);
  event_add(in->event, &in->delay);
}

static struct event* init(struct event_base* base, in_port_t port, struct in6_addr *addr, struct timeval *delay, const uint64_t count, const int size, const char* interface, uint32_t scope) {
  struct udp_io_t* buffer;
  struct ifreq iface;

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

  if (interface != NULL) {
    if (setsockopt(buffer->fd, SOL_SOCKET, SO_BINDTODEVICE, interface, strlen(interface)) < 0) {
      printf("Unable to bind to device (You need to be root to do that ... do you really want to bind to this interface ?)\n");
      PERROR("setsockopt(SO_BINDTODEVICE)")
      return NULL;
    }
  }

  if (scope != 0) {
    buffer->addr.sin6_scope_id = scope;
  }

  buffer->count = count;
  buffer->packet_len = size;
  buffer->addr.sin6_family = AF_INET6;
  buffer->addr.sin6_port   = htons(port);
  memcpy(&buffer->delay, delay, sizeof(struct timeval));
  memcpy(&buffer->addr.sin6_addr, addr, sizeof(struct in6_addr));

  /* Init event */
  buffer->event = event_new(base, -1, 0, &event_cb, buffer);
  event_add(buffer->event, &buffer->delay);
  return buffer->event;
}

static void down(int sig)
{
  assert(gbase != NULL);
  assert(glisten != NULL);
  event_del(glisten);
  event_free(glisten);
  event_base_loopbreak(gbase);
  event_base_free(gbase);
}

/* Default Values */
#define DEFAULT_FILE stdout
#define DEFAULT_PORT 10101
#define DEFAULT_ADDRESS "127.0.0.1"
#define DEFAULT_TIME_SECOND 0
#define DEFAULT_TIME_NANOSECOND 100000
#define DEFAULT_COUNT 0
#define DEFAULT_SIZE 900

static void usage(int err)
{
  printf("listen: Listen on a given socket and print packets content\n");
  printf("Usage: ./listen [OPTIONS]\n");
  printf("Options:\n");
  printf(" -h, --help           Print this ...\n");
  printf(" -d, --dest   <addr>  Specify the destination address (default : %s )\n", DEFAULT_ADDRESS);
  printf(" -p, --port   <port>  Specify the destination port (default : %"PRIu16" )\n", DEFAULT_PORT);
  printf(" -s, --sec    <sec>   Specify the interval in second between two send (default : %i )\n", DEFAULT_TIME_SECOND);
  printf(" -n, --nsec   <nsec>  Specify the interval in nanosecond between two send destination port (default : %i )\n", DEFAULT_TIME_NANOSECOND);
  printf(" -c, --count  <uint>  Specify the starting count of the outgoing packets (default : %i )\n", DEFAULT_COUNT);
  printf(" -l, --size   <size>  Specify the size of outgoing packets (default : %i )\n", DEFAULT_SIZE);
  printf(" -i, --bind   <name>  Specify the interface to bind one (default : no bind)\n");
  exit(err);
}

static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"dest",        required_argument, 0,  'd' },
  {"port",        required_argument, 0,  'p' },
  {"sec",         required_argument, 0,  's' },
  {"nsec",        required_argument, 0,  'n' },
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
  delay.tv_usec = DEFAULT_TIME_NANOSECOND;
  uint64_t count = DEFAULT_COUNT;
  int size = DEFAULT_SIZE;
  uint32_t scope = 0;

  while((opt = getopt_long(argc, argv, "hd:p:s:n:c:l:i:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage(0);
        return 0;
      case 'd':
        addr_s = optarg;
        break;
      case 'p':
        if (port != DEFAULT_PORT) {
          usage(1);
        }
        sscanf(optarg, "%"SCNu16, &port);
        break;
      case 's':
        if (delay.tv_sec != DEFAULT_TIME_SECOND) {
          usage(1);
        }
        sscanf(optarg, "%ld", &delay.tv_sec);
        break;
      case 'n':
        if (delay.tv_usec != DEFAULT_TIME_NANOSECOND) {
          usage(1);
        }
        sscanf(optarg, "%li", &delay.tv_usec);
        break;
      case 'c':
        if (count != DEFAULT_COUNT) {
          usage(1);
        }
        sscanf(optarg, "%"SCNu64, &count);
        break;
      case 'l':
        if (size != DEFAULT_SIZE) {
          usage(1);
        }
        sscanf(optarg, "%i", &size);
        break;
      case 'i':
        interface = optarg;
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

  gbase = event_base_new();
  if (gbase == NULL) {
    PRINTF("Unable to create base (libevent)\n")
    return -1;
  }

  if (scope) {
    glisten = init(gbase, port, &addr, &delay, count, size, NULL, scope);
  } else {
    glisten = init(gbase, port, &addr, &delay, count, size, interface, 0);
  }
  if (glisten == NULL) {
    PRINTF("Unable to create listening event (libevent)\n")
    return -2;
  }

  signal(SIGINT, down);
  event_base_dispatch(gbase);

  return 0;
}
