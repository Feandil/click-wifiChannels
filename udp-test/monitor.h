#ifndef MONITOR_H
#define MONITOR_H

#include <netinet/in.h>
#include <linux/if_packet.h>

/**
 * Open a new monitoring interface
 * Relies on the nl802.11 API
 * @param interface Name of the new interface
 * @param phy_inter Index of the physical interface on which we want to open a monitoring one
 * @return 0 in case of success, negative values for failures
 */
int open_monitor_interface(const char *interface, const uint32_t phy_inter);

/**
 * Close a monitoring interface
 * Relies on the nl802.11 API
 * @param interface Name of the interface
 * @return 0
 */
int close_interface(const char *interface);

/**
 * Number of IPv6 that can be considered as "local"
 */
#define MAX_ADDR         3

/**
 * Buffer size for incoming frames
 */
#define MON_BUF_SIZE  2048

/**
 * Buffer size for controle headers
 */
#define CONTROL_SIZE   512

/**
 * Control structure for receiving control header from kernel
 */
struct control {
  struct cmsghdr cm;          //!< Header
  char control[CONTROL_SIZE]; //!< Data
};

/**
 * Internal "opaque" structure for monitor_listen_on and read_and_parse_monitor
 */
struct mon_io_t {
  struct in6_addr multicast;         //!< Destination that we are monitoring
  struct in6_addr ip_addr[MAX_ADDR]; //!< List of local addresses on the monitored interface
  struct sockaddr_ll ll_addr;        //!< Cache for the source address of incoming packets
  unsigned char hw_addr[6];          //!< MAC address of this node
  in_port_t port;                    //!< Port that we are monitoring
  int fd;                            //!< Socket we are using for monitoring
  unsigned char buf[MON_BUF_SIZE];   //!< Buffer for incoming frames
  struct msghdr hdr;                 //!< Buffer for the recvmsg
  struct control ctrl;               //!< Buffer for the recvmsg
};

/**
 * Callback for the read_and_parse_monitor function
 * @param stamp     Timestamp of the reception
 * @param rate      Rate at which the packet was received, in 0.5Mb/s.
 * @param signal    Signal at which the packet was received, in dBm.
 * @param from      IPv6 address of the sender.
 * @param data      Pointer to the memory zone containing the content of the packet.
 * @param len       Length of this memory zone.
 * @param machdr_fc Flags of the received frame (can contain for example the "Retry" flag).
 * @param arg       Pointer that was passed to the read_and_parse_monitor invocation.
 */
typedef void (*consume_mon_message) (struct timespec *stamp, uint8_t rate, int8_t signal, const struct in6_addr *from, const char* data, size_t len, uint16_t machdr_fc, void* arg);

/**
 * Read a packet from a montoring interface
 * Check the packet integrity, bind() equivalent, transmit UDP data to subfunction
 * @param in      Opaque structure describing the monitoring interface
 * @param consume Subfunction that will handle the data
 * @param arg     Argument to be transmitted directly to the subfunction
 */
void read_and_parse_monitor(struct mon_io_t *in, consume_mon_message consume, void* arg);

struct mon_io_t* monitor_listen_on(struct mon_io_t* mon, in_port_t port, const char* mon_interface, const uint32_t phy_interface, const char* wan_interface, const struct in6_addr* multicast, char first);
#endif /* MONITOR_H */
