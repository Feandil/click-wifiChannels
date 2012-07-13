#ifndef MONITOR_H
#define MONITOR_H

#include <netinet/in.h>
#include <linux/if_packet.h>

int open_monitor_interface(const char *interface, const int phy_inter);
int close_interface(const char *interface);

/* mon buffers */
#define MAX_ADDR         3
#define BUF_SIZE      2048
#define CONTROL_SIZE   512

struct control {
  struct cmsghdr cm;
  char control[CONTROL_SIZE];
};

struct mon_io_t {
  struct in6_addr multicast;
  struct in6_addr ip_addr[MAX_ADDR];
  struct sockaddr_ll ll_addr;
  unsigned char hw_addr[6];
  in_port_t port;
  int fd;
  char buf[BUF_SIZE];
  struct msghdr hdr;
  struct control ctrl;
};


typedef void (*consume_mon_message) (struct timespec *stamp, uint8_t rate, int8_t signal, const struct in6_addr *from, const char* data, ssize_t len, uint16_t machdr_fc, void* arg);
void read_and_parse_monitor(struct mon_io_t *in, consume_mon_message consume, void* arg);
struct mon_io_t* monitor_listen_on(struct mon_io_t* mon, in_port_t port, const char* mon_interface, const int phy_interface, const char* wan_interface, const struct in6_addr* multicast, char first);
#endif /* MONITOR_H */
