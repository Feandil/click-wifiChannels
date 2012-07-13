#include "monitor.h"

#include <assert.h>
#include <errno.h>
#include <linux/nl80211.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "debug.h"
#include "crc.h"
#include "radiotap-parser.h"
#include "network_header.h"

static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err, void *arg)
{
  int *ret = arg;
  *ret = err->error;
  return NL_STOP;
}
static int finish_handler(struct nl_msg *msg, void *arg)
{
  int *ret = arg;
  *ret = 0;
  return NL_SKIP;
}
static int ack_handler(struct nl_msg *msg, void *arg)
{
  int *ret = arg;
  *ret = 0;
  return NL_STOP;
}

typedef int (*set_80211_message) (struct nl_msg *nlmsg, const int nlid, const char* arg_char, const int arg_int);

static int
create_monitor_interface(struct nl_msg *nlmsg, const int nlid, const char* arg_char, const int arg_int)
{
  int ret;

  genlmsg_put(nlmsg, 0, 0, nlid, 0, 0, NL80211_CMD_NEW_INTERFACE , 0);
  ret = nla_put_u32(nlmsg, NL80211_ATTR_WIPHY, arg_int);
  if (ret < 0) {
    fprintf(stderr, "Failed to construct message (WIPHY) : %i\n", ret);
    return ret;
  }
  ret = nla_put_string(nlmsg, NL80211_ATTR_IFNAME, arg_char);
  if (ret < 0) {
    fprintf(stderr, "Failed to construct message (IFNAME) : %i\n", ret);
    return ret;
  }
  ret = nla_put_u32(nlmsg, NL80211_ATTR_IFTYPE, NL80211_IFTYPE_MONITOR);
  if (ret < 0) {
    fprintf(stderr, "Failed to construct message (IFTYPE) : %i\n", ret);
    return ret;
  }
  return 0;
}

static int
delete_interface(struct nl_msg *nlmsg, const int nlid, const char* arg_char, const int arg_int)
{
  int ret;
  unsigned if_id;

  if_id = if_nametoindex(arg_char);
  if (if_id == 0) {
    fprintf(stderr, "Interface deletion error: no such interface (%s)\n", arg_char);
    return -1;
  }

  genlmsg_put(nlmsg, 0, 0, nlid, 0, 0, NL80211_CMD_DEL_INTERFACE , 0);
  ret = nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, if_id);
  if (ret < 0) {
    fprintf(stderr, "Failed to construct message (IFINDEX) : %i\n", ret);
    return ret;
  }
  return 0;
}

static int
send_nl80211_message(set_80211_message content, const char* arg_char, const int arg_int)
{
  /* nl80211 messaging structures */
  struct nl_sock *nlsock = NULL;
  struct nl_msg *nlmsg = NULL;
  struct nl_cb *nlcb = NULL;
  int nlid;

  /* Return Value */
  int ret;

  /* Create a monitor interface : */
  /* Open a socket */
  nlsock = nl_socket_alloc();
  if (!nlsock) {
     fprintf(stderr, "Failed to allocate netlink socket.\n");
     return -ENOMEM;
  }
  if (genl_connect(nlsock)) {
    fprintf(stderr, "Failed to connect to generic netlink.\n");
    ret = -ENOLINK;
    goto message_socket_clean;
  }
  nlid = genl_ctrl_resolve(nlsock, "nl80211");
  if (nlid < 0) {
    fprintf(stderr, "nl80211 not found.\n");
    ret = -ENOENT;
    goto message_socket_clean;
  }
  /* Allocate message and callback */
  nlmsg = nlmsg_alloc();
  if (!nlmsg) {
    fprintf(stderr, "Failed to allocate netlink msg.\n");
    ret = -ENOMEM;
    goto message_socket_clean;
  }
  nlcb = nl_cb_alloc(NL_CB_DEFAULT);
  if (!nlcb) {
    fprintf(stderr, "Failed to allocate netlink callback.\n");
    ret = -ENOMEM;
    goto message_msg_clean;
  }
  /* Create the request */
  ret = (*content)(nlmsg, nlid, arg_char, arg_int);
  if (ret < 0) {
    goto message_cb_clean;
    return ret;
  }
  /* Set the callbacks */
  nl_socket_set_cb(nlsock, nlcb);
  ret = nl_send_auto_complete(nlsock, nlmsg);
  if (ret < 0) {
    fprintf(stderr, "Failed to sent message : %i\n", ret);
    goto message_cb_clean;
    return ret;
  }
  ret = 1;
  nl_cb_err(nlcb, NL_CB_CUSTOM, error_handler, &ret);
  nl_cb_set(nlcb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
  nl_cb_set(nlcb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);

  /* The callbacks set ret <= 0: */
  while (ret > 0) {
    nl_recvmsgs(nlsock, nlcb);
  }

message_cb_clean:
  nl_cb_put(nlcb);
message_msg_clean:
    nlmsg_free(nlmsg);
message_socket_clean:
    nl_socket_free(nlsock);

  /* Return in case of failure */
  if (ret < 0) {
    fprintf(stderr, "Unable to create monitor interface: %s (%i)\n", strerror(-ret), ret);
    return ret;
   }
  return 0;
}


int
open_monitor_interface(const char *interface, const int phy_inter) {

  /* Return Value */
  int ret;

  /* Ioctl structs */
  int sockfd;
  struct ifreq ifreq;

  ret = send_nl80211_message(create_monitor_interface, interface, phy_inter);
  if (ret < 0) {
    return ret;
  }

  sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    fprintf(stderr, "Unable to open a socket (for ioctl): ");
    perror("socket");
    ret = sockfd;
    goto delete_open_interface;
  }
  strncpy(ifreq.ifr_name, interface, IFNAMSIZ);
  /* Read interface flags */
  ret = ioctl(sockfd, SIOCGIFFLAGS, &ifreq);
  if (ret < 0) {
    fprintf(stderr, "Unable to get current flags: ");
    perror("ioctl(SIOCGIFFLAGS)");
    ret = -1;
    goto close_socket;
  }
  /* Change flag and set it */
  ifreq.ifr_flags |= IFF_UP;
  ret = ioctl(sockfd, SIOCSIFFLAGS, &ifreq);
  if (ret < 0) {
    fprintf(stderr, "Unable to set up flag: ");
    perror("ioctl(SIOCSIFFLAGS)");
    ret = -1;
    goto close_socket;
  }

  return 0;

close_socket:
  close(sockfd);
delete_open_interface:
  send_nl80211_message(delete_interface, interface, 0);
  return ret;
}

int
close_interface(const char *interface)
{
  /* Ioctl structs */
  int sockfd;
  struct ifreq ifreq;


  sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    fprintf(stderr, "Unable to open a socket (for ioctl): ");
    perror("socket");
    goto delete_open_interface;
  }
  strncpy(ifreq.ifr_name, interface, IFNAMSIZ);
  /* Read interface flags */
  if (ioctl(sockfd, SIOCGIFFLAGS, &ifreq) < 0) {
    fprintf(stderr, "Unable to get current flags: ");
    perror("ioctl(SIOCGIFFLAGS)");
    goto close_socket;
  }
  /* If opened : change flag and unset it */
  if (ifreq.ifr_flags & IFF_UP) {
    ifreq.ifr_flags ^= IFF_UP;
    if (ioctl(sockfd, SIOCSIFFLAGS, &ifreq) < 0) {
      fprintf(stderr, "Unable to set up flag: ");
      perror("ioctl(SIOCSIFFLAGS)");
      goto close_socket;
    }
  }

close_socket:
  close(sockfd);
delete_open_interface:
  send_nl80211_message(delete_interface, interface, 0);
  return 0;
}

#define READ_CB_MATCH_LOCAL  0x01
#define READ_CB_MATCH_MULTI  0x02

void read_and_parse_monitor(struct mon_io_t *in, consume_mon_message consume, void* arg)
{
  int tmp;
  char match;
  ssize_t len;
  uint16_t radiotap_len;
  struct cmsghdr *chdr;
  struct timespec date;
  struct timespec *stamp;
  struct iovec iov;
  struct ieee80211_radiotap_iterator iterator;
  struct ieee80211_radiotap_header* hdr;
  const struct header_80211 *machdr;
  const struct header_llc   *llchdr;
  const struct header_ipv6  *iphdr;
  const struct header_udp   *udphdr;

  assert(in != NULL);

  memset(&in->hdr, 0, sizeof(struct msghdr));
  in->hdr.msg_iov = &iov;
  in->hdr.msg_iovlen = 1;
  iov.iov_base = in->buf;
  iov.iov_len = BUF_SIZE;
  in->hdr.msg_control = &in->ctrl;
  in->hdr.msg_controllen = sizeof(struct control);
  in->hdr.msg_name = (caddr_t)&(in->ll_addr);
  in->hdr.msg_namelen = sizeof(struct sockaddr_ll);
  stamp = &date;

  len = recvmsg(in->fd, &in->hdr, MSG_DONTWAIT);
  if (len < 0) {
    PERROR("recvmsg")
  } else if (len == 0) {
    PRINTF("Connection Closed\n")
  } else {
    tmp = clock_gettime(CLOCK_MONOTONIC, stamp);
    assert(tmp == 0);
    for (chdr = CMSG_FIRSTHDR(&in->hdr); chdr; chdr = CMSG_NXTHDR(&in->hdr, chdr)) {
      if ((chdr->cmsg_level == SOL_SOCKET)
           && (chdr->cmsg_type == SO_TIMESTAMPING)) {
        stamp = (struct timespec*) CMSG_DATA(chdr);
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

    (*consume)(stamp, rate, signal, (struct in6_addr*)iphdr->src, (char*) (udphdr + 1), len, machdr->fc, arg);
  }
}

