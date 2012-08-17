#include <assert.h>
#include <arpa/inet.h>
#include <ev.h>
#include <getopt.h>
#include <inttypes.h>
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
#include "debug.h"

/** @file client.c Main system of the tool that sends packets */

/**
 * Buffer size for outgoing packets
 */
#define BUF_SIZE 1500
/**
 * Buffer size for transforming binary date to string
 */
#define TIME_SIZE 128

/**
 * Buffer size for controle headers
 */
#define CONTROL_SIZE 512
/**
 * Control structure for receiving control header from kernel
 */
struct control {
  struct cmsghdr cm;          //!< Header
  char control[CONTROL_SIZE]; //!< Data
};

/**
 * Internal data to be passed to the sender process
 */
struct client_buffer {
  int fd;                   //!< Socket used for sending packets
  int packet_len;           //!< Artificial length for packets
  int train_size;           //!< Number of packet to send per counter
  struct sockaddr_in6 addr; //!< Description of the destination (address, port and scope)
  uint64_t count;           //!< Counter of the next packet to be sent
  char buf[BUF_SIZE];       //!< Buffer for outgoing packets
};

// Libev related variables

/**
 * Event loop containing all the events.
 */
struct ev_loop      *event_loop;

/**
 * Special event used for graceful endings.
 * Unrolles all the other events, thus ends a "ev_loop(event_loop, 0);" call.
 */
struct ev_timer   *event_killer;

/**
 * Periodic event that sends the packets.
 */
struct ev_periodic   *send_mess;

/**
 * Callback of the event_killer event.
 * Unrolles all the other events, thus ends a "ev_loop(event_loop, 0);" call.
 * Standard prototype for libev's ev_timer callback.
 * @param loop    Event loop which needs to be stopped.
 * @param w       Event that tiggered this call, should be 'event_killer'.
 * @param revents Causes of this call.
 */
static void
event_end(struct ev_loop *loop, struct ev_timer *w, int revents)
{
  ev_unloop(event_loop, EVUNLOOP_ALL);
}


/**
 * Callback of the send_mess event.
 * Send a train of packets
 * Standard prototype for libev's ev_periodic callback.
 * @param loop     Event loop on which this event is.
 * @param periodic Event that tiggered this call, should be 'send_mess'.
 * @param revents  Causes of this call.
 */
static void
event_cb(struct ev_loop *loop, ev_periodic *periodic, int revents)
{
  ssize_t len, sent_len;
  int i;
  struct client_buffer* buffer;

  /* Retreive the buffer from the event */
  buffer = (struct client_buffer*) periodic->data;
  assert(buffer != NULL);

  /* Prepare the packet */
  len = snprintf(buffer->buf, BUF_SIZE, ",%f,%"PRIu64"|", ev_time(), buffer->count);
  assert(len > 0);
  if (len <= buffer->packet_len) {
    len = buffer->packet_len;
  } else {
    --len;
  }

  /* Send the packet <train> times */
  for (i = 0; i < buffer->train_size; ++i) {
    /* The previous checks and modifications assures that len >= 0 */
    sent_len = sendto(buffer->fd, buffer->buf, (size_t)len, 0, (struct sockaddr *)&buffer->addr, sizeof(struct sockaddr_in6));
    if (sent_len == -1) {
      PERROR("sendto")
      return;
    }
    assert(sent_len == len);
  }

  /* Update the counter for the next run */
  ++buffer->count;
}

/**
 * Initializer of the send_mess periodic event.
 * @param port       Destination UDP port.
 * @param addr       Destibation IPv6 address.
 * @param offset     Offset of the periodic event.
 * @param delay      Delay of the periodic event.
 * @param count      Starting counter for outgoing packets
 * @param size       Minimum size of the outgoing packets
 * @param interface  Interface to bind on (only if scope is 0).
 * @param scope "    Scope" of the IPv6 address if it's a link address (index of the corresponding interface).
 * @param train_size Number of packet to be sent per counter
 * @return Periodic event
 */
static struct ev_periodic*
init(in_port_t port, struct in6_addr *addr, double offset, double delay, const uint64_t count, const int size, const char* interface, uint32_t scope, int train_size)
{
  struct ev_periodic* event;
  struct client_buffer* buffer;

  /* Create buffer */
  buffer = calloc(1, sizeof(struct client_buffer));
  if (buffer == NULL) {
    PRINTF("Unable to use malloc\n")
    return NULL;
  }

  /* Create socket */
  if ((buffer->fd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
    PERROR("socket")
    return NULL;
  }

  /* Bind to an interface if we were asked to */
  if (scope == 0) {
    if (interface != NULL) {
      if (setsockopt(buffer->fd, SOL_SOCKET, SO_BINDTODEVICE, interface, strlen(interface)) < 0) {
        printf("Unable to bind to device (You need to be root to do that ... do you really want to bind to this interface ?)\n");
        PERROR("setsockopt(SO_BINDTODEVICE)")
        return NULL;
      }
    }
  } else {
    buffer->addr.sin6_scope_id = scope;
  }

  /* Store the destination */
  buffer->addr.sin6_family = AF_INET6;
  buffer->addr.sin6_port   = htons(port);
  memcpy(&buffer->addr.sin6_addr, addr, sizeof(struct in6_addr));

  /* Store variables that modify the comportement of the sending algorithm */
  buffer->count = count;
  buffer->packet_len = size;
  buffer->train_size = train_size;

  /* Init event */
  event = (struct ev_periodic*) malloc(sizeof(struct ev_periodic));
  if (buffer == NULL) {
    PRINTF("Unable to use malloc\n")
    close(buffer->fd);
    free(buffer);
    return NULL;
  }
  ev_periodic_init(event, event_cb, offset, delay, NULL);
  event->data = buffer;
  ev_periodic_start(event_loop, event);
  return event;
}

/**
 * Callback for signals.
 * Use the event_killer event to stop the event loop.
 * @param sig Signal received.
 */
static void
down(int sig)
{
  ev_timer_set(event_killer, 0, 0);
  ev_timer_start(event_loop, event_killer);
}

/* Default Values */
//! Default output "file"
#define CLIENT_DEFAULT_FILE stdout
//! Default destination port
#define CLIENT_DEFAULT_PORT 10101
//! Default destination address
#define CLIENT_DEFAULT_ADDRESS "::1"
//! Default interval in seconds between two trains of packets
#define CLIENT_DEFAULT_TIME_SECOND 0
//! Default interval in milliseconds between two trains of packets
#define CLIENT_DEFAULT_TIME_MILLISECOND 20
//! Default starting counter
#define CLIENT_DEFAULT_COUNT 0
//! Default packet size
#define CLIENT_DEFAULT_SIZE 900
//! Default train (number of packet sent by counter) size
#define CLIENT_DEFAULT_TRAIN 1

/**
 * Print a short howto and exit.
 * @param err Execution code to return.
 * @param name Name under which this program was called.
 */
static void
usage(int err, char *name)
{
  printf("%s: Send packets to the given destination\n", name);
  printf("Usage: %s [OPTIONS]\n", name);
  printf("Options:\n");
  printf(" -h, --help           Print this ...\n");
  printf(" -d, --dest   <addr>  Specify the destination address (default: %s)\n", CLIENT_DEFAULT_ADDRESS);
  printf(" -p, --port   <port>  Specify the destination port (default: %"PRIu16")\n", CLIENT_DEFAULT_PORT);
  printf(" -s, --sec    <sec>   Specify the interval in second between two trains of packets (default: %i)\n", CLIENT_DEFAULT_TIME_SECOND);
  printf(" -m, --msec   <msec>  Specify the interval in millisecond between two trains of packets (default: %i)\n", CLIENT_DEFAULT_TIME_MILLISECOND);
  printf(" -c, --count  <uint>  Specify the starting count of the outgoing packets (default: %i)\n", CLIENT_DEFAULT_COUNT);
  printf(" -l, --size   <size>  Specify the size of outgoing packets (default: %i)\n", CLIENT_DEFAULT_SIZE);
  printf(" -i, --bind   <name>  Specify the interface to bind one (default: no bind)\n");
  printf(" -t, --train  <size>  Send trains of <size> packets every sending event (default: %i)\n", CLIENT_DEFAULT_TRAIN);
  exit(err);
}

/**
 * Long options used by getopt_long; see 'usage' for more detail.
 */
static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"dest",        required_argument, 0,  'd' },
  {"port",        required_argument, 0,  'p' },
  {"sec",         required_argument, 0,  's' },
  {"msec",        required_argument, 0,  'm' },
  {"count",       required_argument, 0,  'c' },
  {"size",        required_argument, 0,  'l' },
  {"bind",        required_argument, 0,  'i' },
  {NULL,                          0, 0,   0  }
};

/**
 * Main function for the client tool.
 * @param argc Argument Count
 * @param argv Argument Vector
 * @return Execution return code
 */
int
main(int argc, char *argv[])
{
  int opt;
  char *addr_s = NULL;
  char *interface = NULL;
  in_port_t port = CLIENT_DEFAULT_PORT;
  struct in6_addr addr = IN6ADDR_LOOPBACK_INIT;
  struct timeval delay;
  delay.tv_sec = CLIENT_DEFAULT_TIME_SECOND;
  delay.tv_usec = CLIENT_DEFAULT_TIME_MILLISECOND;
  uint64_t count = CLIENT_DEFAULT_COUNT;
  int size = CLIENT_DEFAULT_SIZE;
  uint32_t scope = 0;
  int trains = CLIENT_DEFAULT_TRAIN;

  while((opt = getopt_long(argc, argv, "hd:p:s:m:c:l:i:t:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage(0, argv[0]);
        return 0;
      case 'd':
        addr_s = optarg;
        break;
      case 'p':
        if (port != CLIENT_DEFAULT_PORT) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%"SCNu16, &port);
        break;
      case 's':
        if (delay.tv_sec != CLIENT_DEFAULT_TIME_SECOND) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%ld", &delay.tv_sec);
        break;
      case 'm':
        if (delay.tv_usec != CLIENT_DEFAULT_TIME_MILLISECOND) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%li", &delay.tv_usec);
        break;
      case 'c':
        if (count != CLIENT_DEFAULT_COUNT) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%"SCNu64, &count);
        break;
      case 'l':
        if (size != CLIENT_DEFAULT_SIZE) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%i", &size);
        break;
      case 'i':
        interface = optarg;
        break;
      case 't':
        if (trains != CLIENT_DEFAULT_TRAIN) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%i", &trains);
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

  if (addr_s != NULL) {
    int tmp = inet_pton(AF_INET6, addr_s, &addr);
    if (tmp == 0) {
      printf("Format de destination invalide\n");
      return -3;
    }
    if (IN6_IS_ADDR_MULTICAST(&addr)) {
      if (!IN6_IS_ADDR_MC_LINKLOCAL(&addr)) {
        printf("Only link-scoped multicast addressess are accepted\n");
        return -3;
      }
      if (interface == NULL) {
        printf("An interface is needed for multicast\n");
        return -3;
      }
      scope = if_nametoindex(interface);
      if (scope == 0) {
        printf("Bad interface name\n");
        return -3;
      }
    }
  }

  event_loop = ev_default_loop (EVFLAG_AUTO);
  if((event_killer = (ev_timer*) malloc(sizeof(ev_timer))) == NULL) {
    PRINTF("Malloc\n")
    return -1;
  }
  ev_init(event_killer, event_end);

  send_mess = init(port, &addr, 0, delay.tv_sec + (((double) delay.tv_usec) / 1000), count, size, interface, scope, trains);
  if (send_mess == NULL) {
    PRINTF("Unable to create sending event\n")
    return -2;
  }

  signal(SIGINT, down);
  signal(SIGQUIT, down);
  signal(SIGABRT, down);
  signal(SIGTERM, down);
  ev_loop(event_loop, 0);

  assert(send_mess->data != NULL);
  free(send_mess->data);
  free(send_mess);
  ev_timer_stop(event_loop, event_killer);
  free(event_killer);
  ev_default_destroy();

  return 0;
}
