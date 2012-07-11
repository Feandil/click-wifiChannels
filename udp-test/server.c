#include <assert.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <inttypes.h>
#include <event.h>
#include <getopt.h>
#include <ifaddrs.h>
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
#include <unistd.h>
#include "crc.h"
#include "debug.h"
#include "monitor.h"
#include "radiotap-parser.h"
#include "network_header.h"
#include "zutil.h"

/* udp buffers */
#define MAX_ADDR         3
#define BUF_SIZE      2048
#define ADDR_BUF_SIZE  128
#define HDR_SIZE        16
#define TIME_SIZE      128
#define CONTROL_SIZE   512

struct control {
  struct cmsghdr cm;
  char control[CONTROL_SIZE];
};
struct udp_io_t {
  struct in6_addr multicast;
  struct in6_addr ip_addr[MAX_ADDR];
  struct sockaddr_ll ll_addr;
  unsigned char hw_addr[6];
  in_port_t port;
  char mon_name[IFNAMSIZ];
  char addr_s[ADDR_BUF_SIZE];
  char buf[BUF_SIZE];
  char date[TIME_SIZE];
  char header[HDR_SIZE];
  struct msghdr hdr;
  struct control ctrl;
  struct zutil zdata;
};

/* Event loop */
struct event_base* gbase;
struct event*      glisten;

#define READ_CB_MATCH_LOCAL  0x01
#define READ_CB_MATCH_MULTI  0x02

static void read_cb(int fd, short event, void *arg) {
  int tmp;
  char match;
  ssize_t len;
  uint16_t radiotap_len;
  struct udp_io_t* in;
  struct cmsghdr *chdr;
  struct timespec *stamp;
  struct timespec date;
  struct iovec iov;
  struct ieee80211_radiotap_iterator iterator;
  struct ieee80211_radiotap_header* hdr;
  const struct header_80211 *machdr;
  const struct header_llc   *llchdr;
  const struct header_ipv6  *iphdr;
  const struct header_udp   *udphdr;
  const char *data;
  const char *end;

  assert(arg != NULL);
  in = (struct udp_io_t*) arg;

  memset(&in->hdr, 0, sizeof(struct msghdr));
  in->hdr.msg_iov = &iov;
  in->hdr.msg_iovlen = 1;
  iov.iov_base = in->buf;
  iov.iov_len = BUF_SIZE;
  in->hdr.msg_control = &in->ctrl;
  in->hdr.msg_controllen = sizeof(struct control);
  in->hdr.msg_name = (caddr_t)&(in->ll_addr);
  in->hdr.msg_namelen = sizeof(struct sockaddr_ll);

  len = recvmsg(fd, &in->hdr, MSG_DONTWAIT);
  if (len < 0) {
    PERROR("recvmsg")
  } else if (len == 0) {
    PRINTF("Connection Closed\n")
  } else {
    tmp = clock_gettime(CLOCK_MONOTONIC, &date);
    assert(tmp == 0);
    tmp = snprintf(in->date, TIME_SIZE, ",%lu.%09li\n", date.tv_sec, date.tv_nsec);
    assert(tmp > 0);
    for (chdr = CMSG_FIRSTHDR(&in->hdr); chdr; chdr = CMSG_NXTHDR(&in->hdr, chdr)) {
      if ((chdr->cmsg_level == SOL_SOCKET)
           && (chdr->cmsg_type == SO_TIMESTAMPING)) {
        stamp = (struct timespec*) CMSG_DATA(chdr);
        tmp = snprintf(in->date, TIME_SIZE, ",%ld.%09ld\n", stamp->tv_sec, stamp->tv_nsec);
        assert(tmp > 0);
      }
    }

    if (in->buf[0] != 0 || in->buf[1] != 0) {
      /* Radiotap starts with 2 zeros */
      return;
    }
    radiotap_len = (((uint16_t) in->buf[3])<<8) + ((uint16_t) in->buf[2]);
    if (radiotap_len > (unsigned) len) {
      /* Packet too short */
      return;
    }
    hdr = (struct ieee80211_radiotap_header*) in->buf;
    if (ieee80211_radiotap_iterator_init(&iterator, hdr , len) != 0) {
      /* Radiotap error */
      return;
    }
    uint8_t rate = 0;
    int8_t signal = 0;
    while ((tmp = ieee80211_radiotap_iterator_next(&iterator, IEEE80211_RADIOTAP_DBM_ANTSIGNAL)) >= 0) {
      switch(tmp) {
        case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
          signal = (int8_t)  *(iterator.arg);
          break;
        case IEEE80211_RADIOTAP_RATE:
          rate = (uint8_t)  *(iterator.arg);
          break;
      }
    }
    machdr = (struct header_80211*) (in->buf + radiotap_len);
    len -= radiotap_len;
    if ((unsigned)len < sizeof(struct header_80211) + sizeof(struct header_llc) + sizeof(struct header_ipv6) + sizeof(struct header_udp)) {
      /* Packet too short */
      return;
    }

    if (((machdr->fc & 0x000c) >> 2) != 0x02) {
      /* Non DATA type */
      return;
    }

    match = 0;
    if (memcmp(machdr->da, in->hw_addr, 6) == 0) {
      match = READ_CB_MATCH_LOCAL;
    }

    if (machdr->da[0] == 0x33 && machdr->da[1] == 0x33 && (memcmp(machdr->da + 2, in->multicast.s6_addr + 12, 4) == 0)) {
      match = READ_CB_MATCH_MULTI;
    }

    if (!match) {
      return;
    }

    if (crc32_80211((uint8_t*) machdr, len - 4) != *((uint32_t*)(((uint8_t*) machdr) + (len - 4)))) {
      /* Bad_ CRC */
      return;
    }

    /* Check if there is a QoS field */
    if ((machdr->fc & 0xf0) == 0x80) {
      llchdr = (struct header_llc*) (((uint8_t*) machdr) + sizeof (struct header_80211) + 2);
      len -= 2;
    } else {
      llchdr = (struct header_llc*) (machdr + 1);
    }
    if (llchdr->type != 0xdd86) {
      /* Non ipv6 traffic */
      return;
    }
    iphdr = (struct header_ipv6*) (llchdr + 1);
    if (iphdr->version != 0x06) {
      /* Non ipv6 traffic */
      return;
    }
    len -= sizeof(struct header_80211) + sizeof(struct header_llc) + sizeof(struct header_ipv6) + sizeof(struct header_udp);

    switch (match) {
      case READ_CB_MATCH_MULTI:
        if (memcmp(iphdr->dst, in->multicast.s6_addr16, 16) != 0) {
          return;
        }
        break;
      case READ_CB_MATCH_LOCAL:
        for (tmp = 0; tmp < MAX_ADDR; ++tmp) {
          if (memcmp(iphdr->dst, in->ip_addr[tmp].s6_addr16, 16) == 0) {
            break;
          }
        }
        if (tmp == MAX_ADDR) {
          return;
        }
        break;
    }

    if (iphdr->next != 0x11) {
      /* Not directly UDP */
      return;
    }

    udphdr = (struct header_udp*) (iphdr + 1);
    if (udphdr->dst_port != in->port) {
      /* Not the good destination */
      return;
    }

    len -= 4; /* Forget the radio footer */

    if (ntohs(udphdr->len) != len + sizeof(struct header_udp)) {
      /* Bad packet : bad length */
      return;
    }

    data = inet_ntop(AF_INET6, iphdr->src, in->addr_s, ADDR_BUF_SIZE);
    assert(data != NULL);
    add_data(&in->zdata, data, strlen(data));

    if ((machdr->fc & 0x0800) == 0x0800) {
      tmp = snprintf(in->header, HDR_SIZE, ",R,%"PRIi8",%"PRIu8, signal, rate);
    } else {
      tmp = snprintf(in->header, HDR_SIZE, ",,%"PRIi8",%"PRIu8, signal, rate);
    }
    assert (tmp > 0);
    add_data(&in->zdata, in->header, tmp);

    data = (char*) (udphdr + 1);
    end = memchr(data, '|', len);
    if (end == NULL) {
      add_data(&in->zdata, data, len);
    } else {
      add_data(&in->zdata, data, end - data);
    }
    add_data(&in->zdata, in->date, strlen(in->date));
  }
}

static struct event* listen_on(struct event_base* base, in_port_t port, const char* mon_interface, const int phy_interface, const char* wan_interface, const struct in6_addr* multicast, FILE* out, int encode) {
  int fd, tmp, so_stamp, tmp_fd;
  struct ifreq ifreq;
  unsigned if_id;
  struct udp_io_t* buffer;
  struct ifaddrs *ifaddr, *head;

  /* Create socket */
  if ((fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
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

  /* Create monitor interface */
  tmp = open_monitor_interface(mon_interface, phy_interface);
  if (tmp < 0) {
    if (tmp == -23) {
      PRINTF("Warning: interface already exist !\n")
    } else {
      PRINTF("Unable to open monitor interface\n")
      return NULL;
    }
  }
  strncpy(buffer->mon_name, mon_interface, IFNAMSIZ);

  if_id = if_nametoindex(mon_interface);
  if (if_id == 0) {
    PRINTF("Monitor interface lost ....\n")
    return NULL;
  }
  /* Bind Socket */
  buffer->ll_addr.sll_family = AF_PACKET;
  buffer->ll_addr.sll_ifindex = if_id;

  if (bind(fd, (struct sockaddr *)&buffer->ll_addr, sizeof(struct sockaddr_ll)) < 0) {
    PERROR("bind()")
    return NULL;
  }

  /* Initialize zlib */
  tmp = zinit(&buffer->zdata, out, encode);
  if (tmp < 0) {
    return NULL;
  }

  /* Timestamp incoming packets */
  so_stamp = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_SYS_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
  if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &so_stamp, sizeof(so_stamp)) < 0) {
    PERROR("setsockopt(SO_TIMESTAMPING)")
    return NULL;
  }

  /* Copy ipv6 addr (multicast)  and port*/
  memcpy(&buffer->multicast, multicast, sizeof(struct in6_addr));
  buffer->port = htons(port);

  /* Find local addresses */
  tmp_fd = socket(AF_INET6, SOCK_DGRAM, 0);
  if (tmp_fd < 0) {
    PERROR("socket")
    return NULL;
  }

  snprintf(ifreq.ifr_name, IFNAMSIZ, "%s", wan_interface);
  if (ioctl(tmp_fd, SIOCGIFHWADDR, (char *)&ifreq) < 0) {
    PERROR("ioctl(sockfd");
    return NULL;
  }
  memcpy(buffer->hw_addr, ifreq.ifr_ifru.ifru_hwaddr.sa_data, 6);
  close(tmp_fd);

  if (getifaddrs(&ifaddr) < 0) {
    PERROR("getifaddrs");
    return NULL;
  }
  tmp = 0;
  for (head = ifaddr; ifaddr != NULL; ifaddr = ifaddr->ifa_next) {
    if (ifaddr->ifa_addr == NULL || ifaddr->ifa_name == NULL) {
      continue;
    }
    if (ifaddr->ifa_addr->sa_family != AF_INET6) {
      continue;
    }
    if (strcmp(ifaddr->ifa_name, wan_interface) == 0) {
      if (tmp >= MAX_ADDR) {
         PRINTF("Too many addr, not able to store them")
         return NULL;
      }
      memcpy(buffer->ip_addr[tmp].s6_addr, ((struct sockaddr_in6*)ifaddr->ifa_addr)->sin6_addr.s6_addr, sizeof(struct in6_addr));
      ++tmp;
    }
  }
  freeifaddrs(head);

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
  close_interface(arg->mon_name);
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

  if (interface != NULL) {
    if (if_nametoindex(interface) == 0) {
      printf("Error, the given interface doesn't exist\n");
      return -1;
    }
  } else {
    if (if_nametoindex(DEFAULT_INTERFACE) == 0) {
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

  signal(SIGINT, down);
  signal(SIGABRT, down);
  signal(SIGQUIT, down);
  signal(SIGTERM, down);
  event_base_dispatch(gbase);

  return 0;
}
