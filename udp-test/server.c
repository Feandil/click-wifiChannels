#include <assert.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <event.h>
#include <getopt.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "zutil.h"
#include "debug.h"

/* udp buffers */
#define BUF_SIZE      OUT_BUF_SIZE
#define ADDR_BUF_SIZE   128
#define TIME_SIZE 128
#define CONTROL_SIZE 512
struct control {
  struct cmsghdr cm;
  char control[CONTROL_SIZE];
};
struct udp_io_t {
  struct sockaddr_in6 addr;
  char addr_s[ADDR_BUF_SIZE];
  char buf[BUF_SIZE];
  char date[TIME_SIZE];
  struct msghdr hdr;
  struct control ctrl;
  struct zutil zdata;
};

/* Event loop */
struct event_base* gbase;
struct event*      glisten;

static void read_cb(int fd, short event, void *arg) {
  int tmp;
  ssize_t len;
  char *end;
  struct udp_io_t* in;
  struct cmsghdr *chdr;
  struct timespec *stamp;
  struct iovec iov;

  assert(arg != NULL);
  in = (struct udp_io_t*) arg;

  memset(&in->hdr, 0, sizeof(struct msghdr));
  in->hdr.msg_iov = &iov;
  in->hdr.msg_iovlen = 1;
  iov.iov_base = in->buf;
  iov.iov_len = BUF_SIZE;
  in->hdr.msg_control = &in->ctrl;
  in->hdr.msg_controllen = sizeof(struct control);
  in->hdr.msg_name = (caddr_t)&(in->addr);
  in->hdr.msg_namelen = sizeof(struct sockaddr_in6);

  len = recvmsg(fd, &in->hdr, MSG_DONTWAIT);
  if (len == -1) {
    PERROR("recvmsg")
  } else if (len == 0) {
    PRINTF("Connection Closed\n")
  } else {
    in->addr_s[0]='\n';
    inet_ntop(AF_INET6, &in->addr.sin6_addr, in->addr_s + 1, ADDR_BUF_SIZE - 1);
    add_data(&in->zdata, in->addr_s, strlen(in->addr_s));
    for (chdr = CMSG_FIRSTHDR(&in->hdr); chdr; chdr = CMSG_NXTHDR(&in->hdr, chdr)) {
      if ((chdr->cmsg_level == SOL_SOCKET)
           && (chdr->cmsg_type == SO_TIMESTAMPING)) {
        stamp = (struct timespec*) CMSG_DATA(chdr);
        tmp = snprintf(in->date, TIME_SIZE, ",%ld.%09ld", stamp->tv_sec, stamp->tv_nsec);
        assert(tmp > 0);
        add_data(&in->zdata, in->date, tmp);
      }
    }
    end = memchr(in->buf, '|', BUF_SIZE);
    if (end == NULL) {
      add_data(&in->zdata, in->buf, len);
    } else {
      add_data(&in->zdata, in->buf, end - in->buf);
    }
  }
}

static struct event* listen_on(struct event_base* base, in_port_t port, FILE* out, int encode, struct ipv6_mreq *mreq) {
  int fd, tmp, so_stamp;
  struct udp_io_t* buffer;

  /* Create socket */
  if ((fd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
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

  /* Bind Socket */
  buffer->addr.sin6_family = AF_INET6;
  buffer->addr.sin6_port   = htons(port);
  if (bind(fd, (struct sockaddr *)&buffer->addr, sizeof(struct sockaddr_in6)) < 0) {
    PERROR("bind()")
    return NULL;
  }

  /* Also listen on Multicast */
  tmp = setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, mreq, sizeof(struct ipv6_mreq));
  if (tmp == -1) {
    PERROR("setsockopt(IPV6_JOIN_GROUP)")
    return NULL;
  }

  /* Initialize zlib */
  tmp = zinit(&buffer->zdata, out, encode);
  if (tmp == -1) {
    return NULL;
  }

  /* Timestamp incoming packets */
  so_stamp = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_SYS_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
  if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &so_stamp, sizeof(so_stamp)) < 0) {
    PERROR("setsockopt(SO_TIMESTAMPING)")
    return NULL;
  }

  /* Init event and add it to active events */
  return event_new(base, fd, EV_READ | EV_PERSIST, &read_cb, buffer);
}

static void down(int sig)
{
  struct udp_io_t* arg;
  assert(gbase != NULL);
  assert(glisten != NULL);
  arg = event_get_callback_arg(glisten);
  assert(arg != NULL);
  end_data(&arg->zdata);
  close(event_get_fd(glisten));
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

static void usage(int err)
{
  printf("listen: Listen on a given socket and print packets content\n");
  printf("Usage: ./listen [OPTIONS]\n");
  printf("Options:\n");
  printf(" -h, --help           Print this ...\n");
  printf(" -o, --ouput  <file>  Specify the output file (default : standard output)\n");
  printf(" -r, --rand           Randomize the output file by adding a random number\n");
  printf(" -l, --level  [0-9]   Specify the level of the output compression (default : %i)\n", DEFAULT_ENCODE);
  printf(" -p, --port   <port>  Specify the port to listen on (default : %"PRIu16" )\n", DEFAULT_PORT);
  printf(" -b           <addr>  Specify the address used for multicast (default : %s)\n", DEFAULT_MULTICAST);
  printf(" -i      <interface>  Specify the interface fot the multicast\n");

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
  int rand = 0;
  int encode = DEFAULT_ENCODE;
  char *filename = NULL;
  char *filetemp;
  in_port_t port = DEFAULT_PORT;
  FILE *dest = DEFAULT_FILE;
  struct ipv6_mreq mreq;
  char *addr_s = NULL;
  char *interface = NULL;
  uint8_t randomized;
  FILE *randsrc;


  while((opt = getopt_long(argc, argv, "hro:p:b:i:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage(0);
        return 0;
      case 'r':
        rand = 1;
        break;
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
      case 'b':
        if (addr_s != NULL) {
          usage(1);
        }
        addr_s = optarg;
        break;
      case 'i':
        if (interface != NULL) {
          usage(1);
        }
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

  if (rand && (filename == NULL)) {
    printf("Unable to randomize the filename as no name was given\n");
    usage(1);
  }

  if (filename != NULL) {
    filetemp = strrchr(filename, '.');
    if ((filetemp == NULL)
        || (strcmp(filetemp, ".gz"))) {
      printf("Bad extension for the output (should be '.gz')\n");
      return -1;
    }
    if (rand) {
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
    if (inet_pton(AF_INET6, addr_s, &mreq.ipv6mr_multiaddr) != 1) {
      printf("Bad address format\n");
      return -1;
    }
    if (!IN6_IS_ADDR_MC_LINKLOCAL(&mreq.ipv6mr_multiaddr)) {
      printf("Error, the address isn't a locallink multicast address\n");
      return -1;
    }
  } else {
     int temp = inet_pton(AF_INET6, DEFAULT_MULTICAST, &mreq.ipv6mr_multiaddr);
     assert(temp == 1);
  }

  if (interface != NULL) {
    mreq.ipv6mr_interface = if_nametoindex(interface);
    if (mreq.ipv6mr_interface == 0) {
      printf("Error, the given interface doesn't exist\n");
      return -1;
    }
  } else {
    mreq.ipv6mr_interface = 0;
  }

  gbase = event_base_new();
  if (gbase == NULL) {
    PRINTF("Unable to create base (libevent)\n")
    return -1;
  }

  glisten = listen_on(gbase, port, dest, encode, &mreq);
  if (glisten == NULL) {
    PRINTF("Unable to create listening event (libevent)\n")
    return -2;
  }
  event_add(glisten, NULL);

  signal(SIGINT, down);
  signal(SIGABRT, down);
  signal(SIGQUIT, down);
  signal(SIGTERM, down);
  event_base_dispatch(gbase);

  return 0;
}
