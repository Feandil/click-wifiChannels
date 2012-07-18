#include <assert.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <event.h>
#include <getopt.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "crc.h"
#include "debug.h"
#include "monitor.h"
#include "radiotap-parser.h"
#include "network_header.h"
#include "zutil.h"

/* udp buffers */
#define ADDR_BUF_SIZE  128
#define HDR_SIZE        16
#define TIME_SIZE      128

struct udp_io_t {
  struct mon_io_t mon;
  char mon_name[IFNAMSIZ];
  char addr_s[ADDR_BUF_SIZE];
  char date[TIME_SIZE];
  char header[HDR_SIZE];
  struct zutil_write zdata;
};

/* Event loop */
struct event_base* gbase;
struct event*      glisten;
struct event*      gdrop;

static void drop_cb(int fd, short event, void *arg) {
  char buffer;
  recv(fd, &buffer, 1, 0);
  PRINTF("DROP\n");
}

static void
consume_data(struct timespec *stamp, uint8_t rate, int8_t signal, const struct in6_addr *from, \
             const char* data, ssize_t len, uint16_t machdr_fc, void* arg)
{
  const char *addr;
  const char *end;
  int tmp;
  struct udp_io_t* in;


  assert(arg != NULL);
  in = (struct udp_io_t*) arg;

  addr = inet_ntop(AF_INET6, from, in->addr_s, ADDR_BUF_SIZE);
  assert(addr != NULL);
  zadd_data(&in->zdata, addr, strlen(addr));

  if ((machdr_fc & 0x0800) == 0x0800) {
    tmp = snprintf(in->header, HDR_SIZE, ",R,%"PRIi8",%"PRIu8, signal, rate);
  } else {
    tmp = snprintf(in->header, HDR_SIZE, ",,%"PRIi8",%"PRIu8, signal, rate);
  }
  assert (tmp > 0);
  zadd_data(&in->zdata, in->header, tmp);

  end = memchr(data, '|', len);
  if (end == NULL) {
    zadd_data(&in->zdata, data, len);
  } else {
    zadd_data(&in->zdata, data, end - data);
  }
  tmp = snprintf(in->date, TIME_SIZE, ",%ld.%09ld\n", stamp->tv_sec, stamp->tv_nsec);
  assert(tmp > 0);
  zadd_data(&in->zdata, in->date, tmp);
}

static void
read_cb(int fd, short event, void *arg)
{
  struct udp_io_t* in;

  assert(arg != NULL);
  in = (struct udp_io_t*) arg;
  read_and_parse_monitor(&in->mon, consume_data, arg);
}

static struct event* listen_on(struct event_base* base, in_port_t port, const char* mon_interface, const int phy_interface, const char* wan_interface, const struct in6_addr* multicast, FILE* out, int encode) {
  int tmp;
  struct udp_io_t* buffer;
  struct mon_io_t* mon;

  /* Create buffer */
  buffer = (struct udp_io_t *)malloc(sizeof(struct udp_io_t));
  if (buffer == NULL) {
    PRINTF("Unable to use malloc\n")
    return NULL;
  }
  memset(buffer, 0, sizeof(struct udp_io_t));

  mon = monitor_listen_on(&buffer->mon, port, mon_interface, phy_interface, wan_interface, multicast, 1);
  if (mon == NULL) {
    PRINTF("Unable to listen on monitoring interface")
    return NULL;
  }
  assert(mon == &buffer->mon);

  strncpy(buffer->mon_name, mon_interface, IFNAMSIZ);

  /* Initialize zlib */
  tmp = zinit_write(&buffer->zdata, out, encode);
  if (tmp < 0) {
    return NULL;
  }

  /* Init event and add it to active events */
  return event_new(base, buffer->mon.fd, EV_READ | EV_PERSIST, &read_cb, buffer);
}

static struct event* drop_on(struct event_base* base, in_port_t port, struct ipv6_mreq *mreq) {
  int fd, tmp;
  struct sockaddr_in6 ipaddr;

  /* Create socket */
  if ((fd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
    PERROR("socket")
    return NULL;
  }

  /* Bind Socket */
  memset(&ipaddr, 0, sizeof(struct sockaddr_in6));
  ipaddr.sin6_family = AF_INET6;
  ipaddr.sin6_port = htons(port);
  if (bind(fd, (struct sockaddr *)&ipaddr, sizeof(struct sockaddr_in6)) < 0) {
    PERROR("bind()")
    return NULL;
  }

  /* Also listen on Multicast */
  tmp = setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, mreq, sizeof(struct ipv6_mreq));
  if (tmp == -1) {
    PERROR("setsockopt(IPV6_JOIN_GROUP)")
    return NULL;
  }

  /* Init event and add it to active events */
  return event_new(base, fd, EV_READ | EV_PERSIST, &drop_cb, NULL);
}

static void down(int sig)
{
  struct udp_io_t* arg;
  assert(gbase != NULL);
  assert(glisten != NULL);
  assert(gdrop != NULL);
  arg = event_get_callback_arg(glisten);
  assert(arg != NULL);
  zend_data(&arg->zdata);
  close_interface(arg->mon_name);
  close(event_get_fd(glisten));
  close(event_get_fd(gdrop));
  event_del(glisten);
  event_free(glisten);
  event_base_loopbreak(gbase);
  event_base_free(gbase);
}

/* Default Values */
#define DEFAULT_FILE stdout
#define DEFAULT_PORT 10101
#define DEFAULT_ENCODE 7
#define DEFAULT_MULTICAST "ff02::1"
#define DEFAULT_INTERFACE "wlan0"

static void usage(int err, char *name)
{
  printf("%s: Listen on a given socket and store timestamped packet content\n", name);
  printf("Usage: %s [OPTIONS]\n", name);
  printf("Options:\n");
  printf(" -h, --help           Print this ...\n");
  printf(" -o, --ouput  <file>  Specify the output file (default: standard output)\n");
  printf(" -r, --rand           Randomize the output file by adding a random number\n");
  printf(" -l, --level  [0-9]   Specify the level of the output compression (default : %i)\n", DEFAULT_ENCODE);
  printf(" -p, --port   <port>  Specify the port to listen on (default: %"PRIu16")\n", DEFAULT_PORT);
  printf(" -b           <addr>  Specify the address used for multicast (default : %s)\n", DEFAULT_MULTICAST);
  printf(" -i      <interface>  Specify the interface to bind on (default : %s)\n", DEFAULT_INTERFACE);

  exit(err);
}

static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"rand",              no_argument, 0,  'r' },
  {"output",      required_argument, 0,  'o' },
  {"level",       required_argument, 0,  'l' },
  {"port",        required_argument, 0,  'p' },
  {NULL,                          0, 0,   0  }
};

int main(int argc, char *argv[]) {
  int opt;
  int randi = 0;
  int encode = DEFAULT_ENCODE;
  char *filename = NULL;
  char *filetemp;
  in_port_t port = DEFAULT_PORT;
  FILE *dest = DEFAULT_FILE;
  char *addr_s = NULL;
  const char *interface = NULL;
  uint8_t randomized;
  FILE *randsrc;
  struct in6_addr multicast;
  struct ipv6_mreq mreq;

  while((opt = getopt_long(argc, argv, "hro:p:b:i:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage(0, argv[0]);
        return 0;
      case 'r':
        randi = 1;
        break;
      case 'o':
        filename = optarg;
        break;
      case 'l':
        if (encode != DEFAULT_ENCODE) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%i", &encode);
        if (encode < 0 || encode > 9) {
          usage(1, argv[0]);
        }
        break;
      case 'm':
        if (port != DEFAULT_PORT) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%"SCNu16, &port);
        break;
      case 'b':
        if (addr_s != NULL) {
          usage(1, argv[0]);
        }
        addr_s = optarg;
        break;
      case 'i':
        if (interface != NULL) {
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

  if (randi && (filename == NULL)) {
    printf("Unable to randomize the filename as no name was given\n");
    usage(1, argv[0]);
  }

  if (filename != NULL) {
    filetemp = strrchr(filename, '.');
    if ((filetemp == NULL)
        || (strcmp(filetemp, ".gz"))) {
      printf("Bad extension for the output (should be '.gz')\n");
      return -1;
    }
    if (randi) {
      *filetemp = '\0';
      filetemp = malloc(strlen(filename) + 8);
      assert(filetemp != NULL);
      randsrc = fopen("/dev/urandom", "r+");
      if (randsrc == NULL) {
        printf("Unable to open /dev/urandom to generate rand\n");
        return -1;
      }
      if (fread(&randomized, 1, sizeof(char), randsrc) != 1) {
        printf("Error loadind random\n");
        PERROR("fread(rand)")
        return -5;
      }
      snprintf(filetemp, strlen(filename) + 8, "%s-%"PRIu8".gz", filename, randomized);
      filename = filetemp;
    }
    dest = fopen(filename, "w");
    if (dest == NULL) {
      printf("Unable to open output file\n");
      return -1;
    }
  }


  if (addr_s != NULL) {
    if (inet_pton(AF_INET6, addr_s, &multicast) != 1) {
      printf("Bad address format\n");
      return -1;
    }
    if (!IN6_IS_ADDR_MC_LINKLOCAL(&multicast)) {
      printf("Error, the address isn't a locallink multicast address\n");
      return -1;
    }
  } else {
     int temp = inet_pton(AF_INET6, DEFAULT_MULTICAST, &multicast);
     assert(temp == 1);
  }

  memcpy(&mreq.ipv6mr_multiaddr, &multicast, sizeof(struct in6_addr));

  if (interface != NULL) {
    if ((mreq.ipv6mr_interface = if_nametoindex(interface)) == 0) {
      printf("Error, the given interface doesn't exist\n");
      return -1;
    }
  } else {
    if ((mreq.ipv6mr_interface = if_nametoindex(DEFAULT_INTERFACE)) == 0) {
      printf("Error, the default interface doesn't exist, please specify one using -i\n");
      return -1;
    }
    interface = DEFAULT_INTERFACE;
  }

  gbase = event_base_new();
  if (gbase == NULL) {
    PRINTF("Unable to create base (libevent)\n")
    return -1;
  }

  glisten = listen_on(gbase, port, "mon0", 0, interface, &multicast, dest, encode);
  if (glisten == NULL) {
    PRINTF("Unable to create listening event (libevent)\n")
    return -2;
  }
  event_add(glisten, NULL);

  gdrop = drop_on(gbase, port, &mreq);
  if (gdrop == NULL) {
    PRINTF("Unable to create listening event (libevent)\n")
    return -2;
  }
  event_add(gdrop, NULL);

  signal(SIGINT, down);
  signal(SIGABRT, down);
  signal(SIGQUIT, down);
  signal(SIGTERM, down);
  event_base_dispatch(gbase);

  return 0;
}
