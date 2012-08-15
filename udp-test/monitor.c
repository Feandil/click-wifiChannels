#include "monitor.h"

#include <assert.h>
#include <errno.h>
#include <ifaddrs.h>
#include <linux/if_ether.h>
#include <linux/net_tstamp.h>
#include <linux/nl80211.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "debug.h"
#include "crc.h"
#include "radiotap-parser.h"
#include "network_header.h"

/**
 * Custom callback from nl802.11, in case of error ('error message processing customization')
 * Sets "*arg" to the error code
 * @param nla netlink address of the peer
 * @param err netlink error message being processed
 * @param arg argument passed on through caller, here shloud be a int*
 * @return NL_STOP: "Stop parsing altogether and discard remaining messages."
 */
static int
error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err, void *arg)
{
  int *ret = arg;
  *ret = err->error;
  return NL_STOP;
}

/**
 * Custom callback from nl802.11, in case of NL_CB_FINISH ("Last message in a series of multi part messages received.")
 * Sets "*arg" to 0
 * msg metlink message being processed
 * arg argument passwd on through caller, here shloud be a int*
 * @return NL_SKIP: "Skip this message. "
 */
static int
finish_handler(struct nl_msg *msg, void *arg)
{
  int *ret = arg;
  *ret = 0;
  return NL_SKIP;
}

/**
 * Custom callback from nl802.11, in case of NL_CB_ACK ("Message is an acknowledge.")
 * Sets "*arg" to 0
 * msg metlink message being processed
 * arg argument passwd on through caller, here shloud be a int*
 * @return NL_STOP: "Stop parsing altogether and discard remaining messages."
 */
static int
ack_handler(struct nl_msg *msg, void *arg)
{
  int *ret = arg;
  *ret = 0;
  return NL_STOP;
}

/**
 * Subfunction for send_nl80211_message: construct a valid nl802.11 message
 * @param nlmsg Message to complete
 * @param nlid numeric family identifier (nl80211 internal thing)
 * @param arg_char char* that was given to send_nl80211_message
 * @param arg_int uint32_t that was given to send_nl80211_message
 * @return 0 in case of success, negative values for failures
 */
typedef int (*set_80211_message) (struct nl_msg *nlmsg, const int nlid, const char* arg_char, const uint32_t arg_int);

/**
 * Fill in a message to create a new monitoring interface
 * Implement set_80211_message
 * @param nlmsg Message to complete
 * @param nlid numeric family identifier (nl80211 internal thing)
 * @param arg_char Name of the monitoring interface that is supposed to be created
 * @param arg_int Index (WIPHY) of the interface which will be monitored
 * @return 0 in case of success, negative values for failures
 */
static int
create_monitor_interface(struct nl_msg *nlmsg, const int nlid, const char* arg_char, const uint32_t arg_int)
{
  int ret;

  /**
   * Set the type of the message: create new interface
   */
  genlmsg_put(nlmsg, 0, 0, nlid, 0, 0, NL80211_CMD_NEW_INTERFACE , 0);

  /**
   * Set the WIPHY of the underlying interface
   */
  ret = nla_put_u32(nlmsg, NL80211_ATTR_WIPHY, arg_int);
  if (ret < 0) {
    fprintf(stderr, "Failed to construct message (WIPHY) : %i\n", ret);
    return ret;
  }

  /**
   * Set the desired name for the new interface
   */
  ret = nla_put_string(nlmsg, NL80211_ATTR_IFNAME, arg_char);
  if (ret < 0) {
    fprintf(stderr, "Failed to construct message (IFNAME) : %i\n", ret);
    return ret;
  }

  /**
   * Set the desired type of interface : monitoring
   */
  ret = nla_put_u32(nlmsg, NL80211_ATTR_IFTYPE, NL80211_IFTYPE_MONITOR);
  if (ret < 0) {
    fprintf(stderr, "Failed to construct message (IFTYPE) : %i\n", ret);
    return ret;
  }
  return 0;
}

/**
 * Fill in a message to delete a monitoring interface
 * Implement set_80211_message
 * @param nlmsg Message to complete
 * @param nlid numeric family identifier (nl80211 internal thing)
 * @param arg_char Name of the monitoring interface that is supposed to be deleted
 * @param arg_int Ingnored
 * @return 0 in case of success, negative values for failures
 */
static int
delete_interface(struct nl_msg *nlmsg, const int nlid, const char* arg_char, const uint32_t arg_int)
{
  int ret;
  unsigned if_id;


  /**
   * Find the index of the interface to be deleted
   */
  if_id = if_nametoindex(arg_char);
  if (if_id == 0) {
    fprintf(stderr, "Interface deletion error: no such interface (%s)\n", arg_char);
    return -1;
  }


  /**
   * Set the type of the message: delete an interface
   */
  genlmsg_put(nlmsg, 0, 0, nlid, 0, 0, NL80211_CMD_DEL_INTERFACE , 0);

  /**
   * Set the index of the interface to be deleted
   */
  ret = nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, if_id);
  if (ret < 0) {
    fprintf(stderr, "Failed to construct message (IFINDEX) : %i\n", ret);
    return ret;
  }
  return 0;
}

/**
 * Sends a 802.11 message
 * @param content Subfunction that will contruct the content of the message
 * @param arg_char char* parameter to be transmitted to subfunction content
 * @param arg_int uint32_t parameter to be transmitted to subfunction content
 * @return 0 in case of success, negative values for failures
 */
static int
send_nl80211_message(set_80211_message content, const char* arg_char, const uint32_t arg_int)
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

/**
 * Open a new monitoring interface
 * Relies on the nl802.11 API
 * @param interface Name of the new interface
 * @param phy_inter Index of the physical interface on which we want to open a monitoring one
 * @return 0 in case of success, negative values for failures
 */
int
open_monitor_interface(const char *interface, const uint32_t phy_inter) {

  /* Return Value */
  int ret;

  /* Ioctl structs */
  int sockfd;
  struct ifreq ifreq;

  /* Send a nl80211 message to create the interface */
  ret = send_nl80211_message(create_monitor_interface, interface, phy_inter);
  if (ret < 0) {
    return ret;
  }

  /* Set the interface UP */
  /* Open a utility socket */
  sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    fprintf(stderr, "Unable to open a socket (for ioctl): ");
    perror("socket");
    ret = sockfd;
    goto delete_open_interface;
  }
  strncpy(ifreq.ifr_name, interface, IF_NAMESIZE);
  /* Read interface flags */
  ret = ioctl(sockfd, SIOCGIFFLAGS, &ifreq);
  if (ret < 0) {
    fprintf(stderr, "Unable to get current flags: ");
    perror("ioctl(SIOCGIFFLAGS)");
    ret = -1;
    goto close_socket;
  }
  /* Set the "IFF_UP" flag */
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

/**
 * Close a monitoring interface
 * Relies on the nl802.11 API
 * @param interface Name of the interface
 * @return 0
 */
int
close_interface(const char *interface)
{
  /* Ioctl structs */
  int sockfd;
  struct ifreq ifreq;

  /* Set the interface DOWN */
  /* Open a utility socket */
  sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    fprintf(stderr, "Unable to open a socket (for ioctl): ");
    perror("socket");
    goto delete_open_interface;
  }
  strncpy(ifreq.ifr_name, interface, IF_NAMESIZE);
  /* Read interface flags */
  if (ioctl(sockfd, SIOCGIFFLAGS, &ifreq) < 0) {
    fprintf(stderr, "Unable to get current flags: ");
    perror("ioctl(SIOCGIFFLAGS)");
    goto close_socket;
  }
  /* Unset the "IFF_UP" flag */
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

/**
 * Internal flag : The message destination was us
 */
#define READ_CB_MATCH_LOCAL  0x01

/**
 * Internal flag : The message destination a some multicast address we are listening on
 */
#define READ_CB_MATCH_MULTI  0x02

/**
 * Read a packet from a montoring interface
 * Check the packet integrity, bind() equivalent, transmit UDP data to subfunction
 * @param in      Opaque structure describing the monitoring interface
 * @param consume Subfunction that will handle the data
 * @param arg     Argument to be transmitted directly to the subfunction
 */
void
read_and_parse_monitor(struct mon_io_t *in, consume_mon_message consume, void* arg)
{
  int tmp;
  char match;
  ssize_t len;
  int radiotap_len;
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
  iov.iov_len = MON_BUF_SIZE;
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
    radiotap_len = (((uint16_t)in->buf[3]) << 8) + ((uint8_t)in->buf[2]);
    if (radiotap_len > len) {
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

    assert(len > 4);
    if (crc32_80211((const uint8_t*) machdr, (size_t)(len - 4)) != *((const uint32_t*)(((const uint8_t*) machdr) + (len - 4)))) {
      /* Bad_ CRC */
      return;
    }

    /* Check if there is a QoS field */
    if ((machdr->fc & 0xf0) == 0x80) {
      llchdr = (const struct header_llc*) (((const uint8_t*) machdr) + sizeof (struct header_80211) + 2);
      len -= 2;
    } else {
      llchdr = (const struct header_llc*) (machdr + 1);
    }
    if (llchdr->type != 0xdd86) {
      /* Non ipv6 traffic */
      return;
    }
    iphdr = (const struct header_ipv6*) (llchdr + 1);
    if (iphdr->version != 0x06) {
      /* Non ipv6 traffic */
      return;
    }
/* sizeof will be smaller that the maximum of ssize_t thus this shouldn't be a problem */
#define SSIZE_OF(a)  ((ssize_t)sizeof(a))
    len -= SSIZE_OF(struct header_80211) + SSIZE_OF(struct header_llc) + SSIZE_OF(struct header_ipv6) + SSIZE_OF(struct header_udp);

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

    udphdr = (const struct header_udp*) (iphdr + 1);
    if (udphdr->dst_port != in->port) {
      /* Not the good destination */
      return;
    }

    len -= 4; /* Forget the radio footer */

    if (ntohs(udphdr->len) != len + SSIZE_OF(struct header_udp)) {
      /* Bad packet : bad length */
      return;
    }
#undef SSIZE_OF

    assert(len > 0);
    (*consume)(stamp, rate, signal, (const struct in6_addr*)iphdr->src, (const char*) (udphdr + 1), (size_t)len, machdr->fc, arg);
  }
}

struct mon_io_t*
monitor_listen_on(struct mon_io_t* mon, in_port_t port, const char* mon_interface, const uint32_t phy_interface, \
                  const char* wan_interface, const struct in6_addr* multicast, char first)
{
  int fd, tmp, so_stamp, tmp_fd;
  struct ifreq ifreq;
  unsigned if_id;
  struct ifaddrs *ifaddr, *head;

  /* Create structure if not present */
  if (mon == NULL) {
    mon = (struct mon_io_t*)malloc(sizeof(struct mon_io_t));
    if (mon == NULL) {
      PRINTF("Unable to use malloc\n")
      return NULL;
    }
  }
  memset(mon, 0, sizeof(struct mon_io_t));

  /* Create socket */
  if ((fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
    PERROR("socket")
    return NULL;
  }
  mon->fd = fd;

  if (first) {
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
  }

  if_id = if_nametoindex(mon_interface);
  if (if_id == 0) {
    PRINTF("Monitor interface lost ....\n")
    return NULL;
  }
  /* Bind Socket */
  mon->ll_addr.sll_family = AF_PACKET;
  assert (((int)if_id) > 0);
  mon->ll_addr.sll_ifindex = (int) if_id;

  if (bind(fd, (struct sockaddr *)&mon->ll_addr, sizeof(struct sockaddr_ll)) < 0) {
    PERROR("bind()")
    return NULL;
  }

  /* Timestamp incoming packets */
  so_stamp = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_SYS_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
  if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &so_stamp, sizeof(so_stamp)) < 0) {
    PERROR("setsockopt(SO_TIMESTAMPING)")
    return NULL;
  }

  /* Copy ipv6 addr (multicast)  and port*/
  memcpy(&mon->multicast, multicast, sizeof(struct in6_addr));
  mon->port = htons(port);

  /* Find local addresses */
  tmp_fd = socket(AF_INET6, SOCK_DGRAM, 0);
  if (tmp_fd < 0) {
    PERROR("socket")
    return NULL;
  }

  snprintf(ifreq.ifr_name, IF_NAMESIZE, "%s", wan_interface);
  if (ioctl(tmp_fd, SIOCGIFHWADDR, (char *)&ifreq) < 0) {
    PERROR("ioctl(sockfd");
    return NULL;
  }
  memcpy(mon->hw_addr, ifreq.ifr_ifru.ifru_hwaddr.sa_data, 6);
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
      memcpy(mon->ip_addr[tmp].s6_addr, ((struct sockaddr_in6*)ifaddr->ifa_addr)->sin6_addr.s6_addr, sizeof(struct in6_addr));
      ++tmp;
    }
  }
  freeifaddrs(head);

  return mon;
}
