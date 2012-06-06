#include <assert.h>
#include <arpa/inet.h>
#include <event.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/net_tstamp.h>
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
#include "debug.h"
#include "zutil.h"

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
  struct event* send;
  struct event* timer;
  int len;
  int packet_len;
  struct timeval delay;
  struct zutil zdata;
  struct sockaddr_in6 addr;
  uint64_t count;
  char buf[BUF_SIZE];
  char buf2[BUF_SIZE];
  char date[TIME_SIZE];
  struct msghdr hdr;
  struct control ctrl;
};

/* Event loop */
struct event_base* gbase;
struct udp_io_t*   working;

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

static void
timestamp_cb(int fd, short event, void *arg)
{
  ssize_t len, tmp;
  struct udp_io_t* in;
  struct cmsghdr *chdr;
  struct timespec *stamp;
  struct iovec iov;

  assert(arg != NULL);
  in = (struct udp_io_t*) arg;

  memset(&in->hdr, 0, sizeof(struct msghdr));
  in->hdr.msg_iov = &iov;
  in->hdr.msg_iovlen = 1;
  iov.iov_base = in->buf2;
  iov.iov_len = BUF_SIZE;
  in->hdr.msg_control = &in->ctrl;
  in->hdr.msg_controllen = sizeof(struct control);

  len = recvmsg(fd, &in->hdr, MSG_DONTWAIT|MSG_ERRQUEUE);
  if (len == -1) {
    PERROR("recvmsg")
    return;
  }
  for (chdr = CMSG_FIRSTHDR(&in->hdr); chdr; chdr = CMSG_NXTHDR(&in->hdr, chdr)) {
    if ((chdr->cmsg_level == SOL_SOCKET)
         && (chdr->cmsg_type == SO_TIMESTAMPING)) {
      stamp = (struct timespec*) CMSG_DATA(chdr);
      tmp = snprintf(in->date, TIME_SIZE, "\n%ld.%09ld,%ld.%09ld,%ld.%09ld", stamp->tv_sec, stamp->tv_nsec, (stamp + 1)->tv_sec, (stamp + 1)->tv_nsec, (stamp + 2)->tv_sec, (stamp + 2)->tv_nsec);
      assert(tmp > 0);
      PRINTF("%.*s\n", tmp, in->date)
      add_data(&in->zdata, in->date, tmp);
      add_data(&in->zdata, in->buf2, len);
    }
  }
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
  event_add(in->send, &in->delay);
}

static struct udp_io_t*
init(struct event_base* base, in_port_t port, struct in6_addr *addr, struct timeval *delay, const uint64_t count, const int size, const char* interface, uint32_t scope, FILE* output)
{
  int so_stamp, tmp;
  struct udp_io_t* buffer;
  struct ifreq req;
  struct hwtstamp_config hwstamp;


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
  memcpy(&buffer->delay, delay, sizeof(struct timeval));
  memcpy(&buffer->addr.sin6_addr, addr, sizeof(struct in6_addr));

  /* If we are recording the timestamps, lets do it */
  if (output != NULL) {
    memset(&req, 0, sizeof(struct ifreq));
    strncpy(req.ifr_name, interface, IF_NAMESIZE - 1);
    req.ifr_data = (void*) &hwstamp;
    memset(&hwstamp, 0, sizeof(struct hwtstamp_config));
    hwstamp.tx_type = HWTSTAMP_TX_ON;
    hwstamp.rx_filter = HWTSTAMP_FILTER_NONE;
    if (ioctl(buffer->fd, SIOCSHWTSTAMP, &req) < 0) {
      printf("Warning: unable to set Hardware timestamp\n");
      PERROR("ioctl(SIOSCHWTSTAMP)")
    }
    so_stamp = SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_SYS_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
    if (setsockopt(buffer->fd, SOL_SOCKET, SO_TIMESTAMPING, &so_stamp, sizeof(so_stamp)) < 0) {
      PERROR("setsockopt(SO_TIMESTAMPING)")
      return NULL;
    }
    /* Initialize zlib */
    tmp = zinit(&buffer->zdata, output, 8);
    if (tmp == -1) {
      return NULL;
    }
    buffer->timer = event_new(base, buffer->fd, EV_READ|EV_PERSIST, &timestamp_cb, buffer);

    if (event_add(buffer->timer, NULL) < 0) {
      return NULL;
    }
  }

  /* Init event */
  buffer->send = event_new(base, -1, 0, &event_cb, buffer);
  event_add(buffer->send, &buffer->delay);
  if (event_add(buffer->send, NULL) < 0) {
    return NULL;
  }
  return buffer;
}

static void down(int sig)
{
  assert(gbase != NULL);
  assert(working != NULL);
  if (working->send != NULL) {
    event_del(working->send);
    event_free(working->send);
  }
  if (working->timer != NULL) {
    end_data(&working->zdata);
    close(event_get_fd(working->timer));
    event_del(working->timer);
    event_free(working->timer);
  }
  event_base_loopbreak(gbase);
  event_base_free(gbase);
}

/* Default Values */
#define DEFAULT_FILE stdout
#define DEFAULT_PORT 10101
#define DEFAULT_ADDRESS "::1"
#define DEFAULT_TIME_SECOND 0
#define DEFAULT_TIME_NANOSECOND 100000
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
  printf(" -u, --usec   <usec>  Specify the interval in microsecond between two packets (default: %i)\n", DEFAULT_TIME_NANOSECOND);
  printf(" -c, --count  <uint>  Specify the starting count of the outgoing packets (default: %i)\n", DEFAULT_COUNT);
  printf(" -l, --size   <size>  Specify the size of outgoing packets (default: %i)\n", DEFAULT_SIZE);
  printf(" -i, --bind   <name>  Specify the interface to bind one (default: no bind)\n");
  printf(" -t, --stamp  <file>  Timestamp the real moment the packets were sent, store information in <file>\n");
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
  {"stamp",       required_argument, 0,  't' },
  {NULL,                          0, 0,   0  }
};

int main(int argc, char *argv[]) {
  int opt;
  char *addr_s = NULL;
  char *interface = NULL;
  char *filename = NULL;
  FILE* output;
  in_port_t port = DEFAULT_PORT;
  struct in6_addr addr = IN6ADDR_LOOPBACK_INIT;
  struct timeval delay;
  delay.tv_sec = DEFAULT_TIME_SECOND;
  delay.tv_usec = DEFAULT_TIME_NANOSECOND;
  uint64_t count = DEFAULT_COUNT;
  int size = DEFAULT_SIZE;
  uint32_t scope = 0;

  while((opt = getopt_long(argc, argv, "hd:p:s:u:c:l:i:t:", long_options, NULL)) != -1) {
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
        if (delay.tv_usec != DEFAULT_TIME_NANOSECOND) {
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
      case 't':
        filename = optarg;
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

  if (filename != NULL) {
    if (interface == NULL) {
      printf("TimeStamping needs an interface\n");
      return -1;
    }
    output = fopen(filename, "w");
    if (output == NULL) {
      printf("Unable to open output file\n");
      return -1;
    }
  } else {
    output = NULL;
  }

  gbase = event_base_new();
  if (gbase == NULL) {
    PRINTF("Unable to create base (libevent)\n")
    return -1;
  }

  working = init(gbase, port, &addr, &delay, count, size, interface, scope, output);
  if (working == NULL) {
    PRINTF("Unable to create listening event (libevent)\n")
    return -2;
  }

  signal(SIGINT, down);
  event_base_dispatch(gbase);

  return 0;
}
