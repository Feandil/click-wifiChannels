#ifndef MONITOR_H
#define MONITOR_H

/** @file monitor.h Functions to simplify the use of a monitoring interface to capture packet with some frame information */

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

/**
 * Create a opaque structure which can be used by read_and_parse_monitor to read packets from a monitoring interface
 * @param mon           Memory zone that will contain the opaque structure. If NULL, the malloc function is called
 * @param port          Port on which bind our listening process
 * @param mon_interface Name of the monitoring interface to use
 * @param phy_interface Index (WIPHY) of the interface to monitor (unimportant if not 'first')
 * @param wan_interface Name of the interface monitored (Used to extract the local addresses)
 * @param multicast     Multicat address to bind on
 * @param first         Indicate if the monitoring interface doesn't exist (true) and thus need to be created
 * @return An opaque structure, containing an socket, that can be used by read_and_parse_monitor. Need to be freed after use if 'mon' was NULL
 */
struct mon_io_t* monitor_listen_on(struct mon_io_t* mon, in_port_t port, const char* mon_interface, const uint32_t phy_interface, const char* wan_interface, const struct in6_addr* multicast, char first);

/**
 * Extract from the opaque structure a non Link-local address for the link.
 * @param mon   Opaque structure describing the monitoring interface.
 * @param my_ip Where to store the IPv6 address.
 * @return 0 if OK, < 0 for errors
 */
int mon_extract_my_ip(struct mon_io_t *mon, struct in6_addr *my_ip);

/**
 * Extract from the opaque structure a link-local address for the link.
 * @param mon   Opaque structure describing the monitoring interface.
 * @param my_ip Where to store the IPv6 address.
 * @return 0 if OK, < 0 for errors
 */
int mon_extract_my_local_ip(struct mon_io_t *mon, struct in6_addr *my_ip);

#endif /* MONITOR_H */
