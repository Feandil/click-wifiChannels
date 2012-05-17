#include <assert.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <event.h>
#include <getopt.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>

#ifdef DEBUG
  #define PERROR(x) perror(x);
  #define PRINTF(...) printf(__VA_ARGS__);
#else
  #define PERROR(x)
  #define PRINTF(...)
#endif

/* udp buffers */
#define BUF_SIZE                        1024
struct udp_io_t {
  int fd;
  struct event* event;
  int len;
  struct timeval delay;
  uint64_t count;
  struct sockaddr_in addr;
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
  len = snprintf(data->buf, BUF_SIZE, "%lu.%li,%"PRIu64, date.tv_sec, date.tv_nsec, data->count);
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
  len = sendto(in->fd, in->buf, in->len, 0, (struct sockaddr *)&in->addr, sizeof(struct sockaddr_in6));
  if (len == -1) {
    PERROR("sendto")
    return;
  }
  assert(len == in->len);
  event_add(in->event, &in->delay);
}

static struct event* init(struct event_base* base, in_port_t port, struct in_addr *addr, struct timeval *delay) {
  struct udp_io_t* buffer;

  /* Create buffer */
  buffer = (struct udp_io_t *)malloc(sizeof(struct udp_io_t));
  if (buffer == NULL) {
    PRINTF("Unable to use malloc\n")
    return NULL;
  }
  memset(buffer, 0, sizeof(struct udp_io_t));

  /* Create socket */
  if ((buffer->fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    PERROR("socket")
    return NULL;
  }

  buffer->addr.sin_family = AF_INET;
  buffer->addr.sin_port   = htons(port);
  memcpy(&buffer->delay, delay, sizeof(struct timeval));
  memcpy(&buffer->addr.sin_addr, addr, sizeof(struct in_addr));
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
  exit(err);
}

static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"dest",        required_argument, 0,  'd' },
  {"port",        required_argument, 0,  'p' },
  {"sec",         required_argument, 0,  's' },
  {"nsec",        required_argument, 0,  'n' },
  {NULL,                          0, 0,   0  }
};

int main(int argc, char *argv[]) {
  int opt;
  char *addr_s = NULL;
  in_port_t port = DEFAULT_PORT;
  struct in_addr addr;
  struct timeval delay;
  delay.tv_sec = DEFAULT_TIME_SECOND;
  delay.tv_usec = DEFAULT_TIME_NANOSECOND;

  while((opt = getopt_long(argc, argv, "hd:p:s:n:", long_options, NULL)) != -1) {
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
    int tmp = inet_aton(addr_s, &addr);
    if (tmp == 0) {
      printf("Format de destination invalide\n");
      return -3;
    }
  } else {
    int tmp = inet_aton(DEFAULT_ADDRESS, &addr);
    assert(tmp != 0);
  }

  gbase = event_base_new();
  if (gbase == NULL) {
    PRINTF("Unable to create base (libevent)\n")
    return -1;
  }

  glisten = init(gbase, port, &addr, &delay);
  if (glisten == NULL) {
    PRINTF("Unable to create listening event (libevent)\n")
    return -2;
  }

  signal(SIGINT, down);
  event_base_dispatch(gbase);

  return 0;
}
