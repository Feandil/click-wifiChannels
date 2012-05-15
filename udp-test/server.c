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
  struct sockaddr_in addr;
  char buf[BUF_SIZE];
  FILE *output;
};

/* Event loop */
struct event_base* gbase;
struct event*      glisten;

static void read_cb(int fd, short event, void *arg) {
  size_t size;
  ssize_t len;
  struct udp_io_t* in;

  assert(arg != NULL);
  in = (struct udp_io_t*) arg;
  size = sizeof(struct sockaddr_in);

  len = recvfrom(fd, in->buf, BUF_SIZE, 0, (struct sockaddr *)&(in->addr), &size);
  if (len == -1) {
    PERROR("recvfrom")
  } else if (len == 0) {
    PRINTF("Connection Closed\n")
  } else {
    fprintf(in->output, "%s,%.*s\n", inet_ntoa(in->addr.sin_addr), len, in->buf);
  }
}

static struct event* listen_on(struct event_base* base, in_port_t port, FILE* out) {
  int fd;
  struct udp_io_t* buffer;

  /* Create socket */
  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    PERROR("socket")
    return NULL;
  }

  /* Create buffer */
  buffer = (struct udp_io_t *)malloc(sizeof(struct udp_io_t));
  if (buffer == NULL) {
    PRINTF("Unable to use malloc\n")
    return NULL;
  }
  memset(buffer, 0, sizeof(struct udp_io_t));
  buffer->output = out;

  /* Bind Socket */
  buffer->addr.sin_family = AF_INET;
  buffer->addr.sin_port   = htons(port);
  if (bind(fd, (struct sockaddr *)&buffer->addr, sizeof(struct sockaddr_in)) < 0) {
    PERROR("bind()")
    return NULL;
  }

  /* Init event and add it to active events */
  return event_new(base, fd, EV_READ | EV_PERSIST, &read_cb, buffer);
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

static void usage(int err)
{
  printf("listen: Listen on a given socket and print packets content\n");
  printf("Usage: ./listen [OPTIONS]\n");
  printf("Options:\n");
  printf(" -h, --help           Print this ...\n");
  printf(" -o, --ouput  <file>  Specify the output file (default : standard output)\n");
  printf(" -p, --port   <port>  Specify the port to listen on (default : %"PRIu16" )\n", DEFAULT_PORT);

  exit(err);
}

static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"output",      required_argument, 0,  'o' },
  {"port",        required_argument, 0,  'p' },
  {NULL,                          0, 0,   0  }
};

int main(int argc, char *argv[]) {
  int opt;
  char *filename = NULL;
  in_port_t port = DEFAULT_PORT;
  FILE *dest = DEFAULT_FILE;

  while((opt = getopt_long(argc, argv, "ho:p:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage(0);
        return 0;
      case 'o':
        filename = optarg;
        break;
      case 'm':
        if (port != DEFAULT_PORT) {
          usage(1);
        }
        sscanf(optarg, "%"SCNu16, &port);
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

  if (filename != NULL) {
    dest = fopen(filename, "w");
    if (dest == NULL) {
      printf("Unable to open output file\n");
      return -1;
    }
  }

  gbase = event_base_new();
  if (gbase == NULL) {
    PRINTF("Unable to create base (libevent)\n")
    return -1;
  }

  glisten = listen_on(gbase, port, dest);
  if (glisten == NULL) {
    PRINTF("Unable to create listening event (libevent)\n")
    return -2;
  }
  event_add(glisten, NULL);

  signal(SIGINT, down);
  event_base_dispatch(gbase);

  return 0;
}
