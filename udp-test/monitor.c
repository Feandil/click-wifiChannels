#include <errno.h>
#include <linux/nl80211.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <net/if.h>
#include <stropts.h>
#include <linux/sockios.h>
#include <unistd.h>

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
  ret = nl_send_auto(nlsock, nlmsg);
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
