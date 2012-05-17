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
#include <zlib.h>

#ifdef DEBUG
  #define PERROR(x) perror(x);
  #define PRINTF(...) printf(__VA_ARGS__);
#else
  #define PERROR(x)
  #define PRINTF(...)
#endif

/* udp buffers */
#define BUF_SIZE      1500
#define OUT_BUF_SIZE  BUF_SIZE
#define ADDR_BUF_SIZE   20
struct udp_io_t {
  struct sockaddr_in addr;
  char addr_s[ADDR_BUF_SIZE];
  char buf[BUF_SIZE];
  char out[OUT_BUF_SIZE];
  z_stream strm;
  FILE *output;
};

/* Event loop */
struct event_base* gbase;
struct event*      glisten;

static void add_data(struct udp_io_t* in, char* data, size_t len) {
  int ret;
  assert(in->strm.avail_in == 0);
  assert(in->strm.avail_out != 0);
  in->strm.next_in = (Bytef *)data;
  in->strm.avail_in = len;
  while (in->strm.avail_in != 0) {
    ret = deflate(&in->strm, Z_NO_FLUSH);
    assert(ret != Z_STREAM_ERROR);
    if (in->strm.avail_out == 0) {
      if (fwrite(in->out, 1, OUT_BUF_SIZE, in->output) != OUT_BUF_SIZE || ferror(in->output)) {
        PRINTF("Unable to write to outputfile\n")
        (void)deflateEnd(&in->strm);
        exit(1);
      }
      in->strm.avail_out = OUT_BUF_SIZE;
      in->strm.next_out = (Bytef *)in->out;
    }
  }
}

static void end_data(struct udp_io_t* in) {
  size_t available;
  int ret;

  assert(in != NULL);
  assert(in->strm.avail_in == 0);
  assert(in->strm.avail_out != 0);
  do {
    available = OUT_BUF_SIZE - in->strm.avail_out;
    if (available != 0) {
      if (fwrite(in->out, 1, available, in->output) != available || ferror(in->output)) {
        PRINTF("Unable to write to outputfile\n")
        (void)deflateEnd(&in->strm);
        exit(1);
      }
      in->strm.avail_out = OUT_BUF_SIZE;
      in->strm.next_out = (Bytef*)in->out;
    }
    ret = deflate(&in->strm, Z_FINISH);
    assert(ret != Z_STREAM_ERROR);
  } while (in->strm.avail_out != OUT_BUF_SIZE);
  (void)deflateEnd(&in->strm);
}

static void read_cb(int fd, short event, void *arg) {
  size_t size;
  ssize_t len;
  char *end;
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
    add_data(in, in->addr_s, snprintf(in->addr_s, ADDR_BUF_SIZE, "\n%s,", inet_ntoa(in->addr.sin_addr) ));
    end = memchr(in->buf, '|', BUF_SIZE);
    if (end == NULL) {
      add_data(in, in->buf, len);
    } else {
      add_data(in, in->buf, end - in->buf);
    }
  }
}

static struct event* listen_on(struct event_base* base, in_port_t port, FILE* out, int encode) {
  int fd, ret;
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

  /* Initialize zlib */
  buffer->strm.zalloc = Z_NULL;
  buffer->strm.zfree = Z_NULL;
  buffer->strm.opaque = Z_NULL;
  ret = deflateInit2(&buffer->strm, encode, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  if (ret != Z_OK) {
    PRINTF("ZLIB initialization error : %i\n", ret)
    return NULL;
  }
  buffer->strm.next_out = (Bytef *)buffer->out;
  buffer->strm.avail_out = OUT_BUF_SIZE;

  /* Init event and add it to active events */
  return event_new(base, fd, EV_READ | EV_PERSIST, &read_cb, buffer);
}

static void down(int sig)
{
  assert(gbase != NULL);
  assert(glisten != NULL);
  end_data(event_get_callback_arg(glisten));
  event_del(glisten);
  event_free(glisten);
  event_base_loopbreak(gbase);
  event_base_free(gbase);
}

/* Default Values */
#define DEFAULT_FILE stdout
#define DEFAULT_PORT 10101
#define DEFAULT_ENCODE 7

static void usage(int err)
{
  printf("listen: Listen on a given socket and print packets content\n");
  printf("Usage: ./listen [OPTIONS]\n");
  printf("Options:\n");
  printf(" -h, --help           Print this ...\n");
  printf(" -o, --ouput  <file>  Specify the output file (default : standard output)\n");
  printf(" -l, --level  [0-9]   Specify the level of the output compression (default : %i)\n", DEFAULT_ENCODE);
  printf(" -p, --port   <port>  Specify the port to listen on (default : %"PRIu16" )\n", DEFAULT_PORT);

  exit(err);
}

static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"output",      required_argument, 0,  'o' },
  {"level",       required_argument, 0,  'l' },
  {"port",        required_argument, 0,  'p' },
  {NULL,                          0, 0,   0  }
};

int main(int argc, char *argv[]) {
  int opt;
  int encode = DEFAULT_ENCODE;
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
      case 'l':
        if (encode != DEFAULT_ENCODE) {
          usage(1);
        }
        sscanf(optarg, "%i", &encode);
        if (encode < 0 || encode > 9) {
          usage(1);
        }
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

  glisten = listen_on(gbase, port, dest, encode);
  if (glisten == NULL) {
    PRINTF("Unable to create listening event (libevent)\n")
    return -2;
  }
  event_add(glisten, NULL);

  signal(SIGINT, down);
  event_base_dispatch(gbase);

  return 0;
}
