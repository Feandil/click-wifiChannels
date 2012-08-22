#include <assert.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <errno.h>
#include <ev.h>
#include <getopt.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "crc.h"
#include "debug.h"
#include "monitor.h"
#include "radiotap-parser.h"
#include "network_header.h"
#include "zutil.h"

/** @file server.c Main system of the tool that store received packets for later statistical analysis */

/**
 * Buffer size for transforming a binary IPv6 into a string
 */
#define ADDR_BUF_SIZE  128

/**
 * Buffer size for putting frame information into a string
 */
#define HDR_SIZE        16

/**
 * Buffer size for transforming binary date to string
 */
#define TIME_SIZE      128

/**
 * Internal data to be passed to the callbacks
 */
struct server_buffer {
  struct mon_io_t mon;        //!< Monitor "opaque" structure for monitor_listen_on and read_and_parse_monito
  char mon_name[IF_NAMESIZE]; //!< Name of the monitoring interface
  char addr_s[ADDR_BUF_SIZE]; //!< Buffer for turning a binary IPv6 into a string
  char date[TIME_SIZE];       //!< Buffer size for transforming binary date to string
  char header[HDR_SIZE];      //!< Buffer putting frame information into a string
  struct zutil_write zdata;   //!< Zutil opaque structure to encode information
  char *filename;             //!< Name of the file currently used for writing the encoded stream
  int encode;                 //!< Zlib encoding level
  int filename_count;         //!< In case of rotated files, index of the current file
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
 * Event linked to a monitoring socket and storing the incoming traffic
 */
struct ev_io         *glisten;
/**
 * Event linked to a normal socket in order to drop receive packet (Block icmp errors)
 */
struct ev_io         *gdrop;
/**
 * Periodic event that rotates the output file
 */
struct ev_periodic   *greload;

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
 * Callback of the gdrop event.
 * Just drop the packet
 * Standard prototype for libev's ev_io callback.
 * @param loop    Event loop on which this event is.
 * @param io      Event that tiggered this call, should be 'recv_mess'.
 * @param revents Causes of this call.
 */
static void
drop_cb(struct ev_loop *loop, ev_io *io, int revents)
{
  char buffer;
  recv(io->fd, &buffer, 1, 0);
  PRINTF("DROP\n");
}

/**
 * Data received callback.
 * Callback of a read_and_parse_monitor (from monitor.h) call, called by read_cb.
 * Store the received information using zutil
 * @param stamp     Timestamp of the reception
 * @param rate      Rate at which the packet was received, in 0.5Mb/s.
 * @param sig       Signal at which the packet was received, in dBm.
 * @param from      IPv6 address of the sender.
 * @param data      Pointer to the memory zone containing the content of the packet.
 * @param len       Length of this memory zone.
 * @param machdr_fc Flags of the received frame (can contain for example the "Retry" flag).
 * @param arg       Pointer that was passed to the read_and_parse_monitor invocation, must contain a mon_io_t structure.
 */
static void
consume_data(struct timespec *stamp, uint8_t rate, int8_t sig, const struct in6_addr *from, \
             const char* data, size_t len, uint16_t machdr_fc, void* arg)
{
  const char *addr;
  const char *end;
  int tmp;
  struct server_buffer* in;

  /* Retrieve the buffer */
  assert(arg != NULL);
  in = (struct server_buffer*) arg;

  /* Retrieve and store the source address */
  addr = inet_ntop(AF_INET6, from, in->addr_s, ADDR_BUF_SIZE);
  assert(addr != NULL);
  zadd_data(&in->zdata, addr, strlen(addr));

  /* If there is a retry flag, store it. Also store signal strength and recieve rate */
  if ((machdr_fc & 0x0800) == 0x0800) {
    tmp = snprintf(in->header, HDR_SIZE, ",R,%"PRIi8",%"PRIu8, sig, rate);
  } else {
    tmp = snprintf(in->header, HDR_SIZE, ",,%"PRIi8",%"PRIu8, sig, rate);
  }
  assert (tmp > 0);
  zadd_data(&in->zdata, in->header, (size_t)tmp);

  /*
   * The packet contains data ended by a '|' and then noise.
   * Extract/store the data and discard the noise.
   */
  assert(len > 0);
  end = memchr(data, '|', len);
  if (end == NULL) {
    zadd_data(&in->zdata, data, len);
  } else {
    assert(end > data);
    zadd_data(&in->zdata, data, (size_t)(end - data));
  }
  /* Add our timestamp at the end */
  tmp = snprintf(in->date, TIME_SIZE, ",%ld.%09ld\n", stamp->tv_sec, stamp->tv_nsec);
  assert(tmp > 0);
  zadd_data(&in->zdata, in->date, (size_t)tmp);
}

/**
 * Callback of the recv_mess event.
 * Use read_and_parse_monitor from monitor.h to analyse the packet,
 * Use subfunction consume_data to analyse the data
 * Standard prototype for libev's ev_io callback.
 * @param loop    Event loop on which this event is.
 * @param io      Event that tiggered this call, should be 'recv_mess'.
 * @param revents Causes of this call.
 */
static void
read_cb(struct ev_loop *loop, ev_io *io, int revents)
{
  struct server_buffer* in;

  in = (struct server_buffer*) io->data;
  assert(in != NULL);
  read_and_parse_monitor(&in->mon, consume_data, in);
}

/**
 * Create the libev io event that listen on the interface.
 * Use read_cb as the callback for that event
 * @param port          Port to bind on
 * @param mon_interface Name of the monitoring interface
 * @param phy_interface Index (WIPHY) of the underlying physical interface to monitor on
 * @param wan_interface Name of the underlying interface to monitor on
 * @param multicast     Address to bind on
 * @param out           Already opened FILE for storing the data
 * @param encode        Level of zlib encoding
 * @param filename      Filename of opened file, used for rotating it
 * @return started libev io event which listen for new packets
 */
static struct ev_io*
listen_on(in_port_t port, const char* mon_interface, const uint32_t phy_interface, const char* wan_interface, \
          const struct in6_addr* multicast, FILE* out, int encode, char* filename)
{
  int tmp;
  struct ev_io* event;
  struct server_buffer* buffer;
  struct mon_io_t* mon;

  /* Create local buffer */
  buffer = calloc(1, sizeof(struct server_buffer));
  if (buffer == NULL) {
    PRINTF("Unable to use malloc\n")
    return NULL;
  }

  /* Initialize the monitoring structure */
  mon = monitor_listen_on(&buffer->mon, port, mon_interface, phy_interface, wan_interface, multicast, 1);
  if (mon == NULL) {
    PRINTF("Unable to listen on monitoring interface")
    return NULL;
  }
  assert(mon == &buffer->mon);

  /* Store various information */
  strncpy(buffer->mon_name, mon_interface, IF_NAMESIZE);
  buffer->filename = filename;
  buffer->encode = encode;

  /* Initialize zlib */
  tmp = zinit_write(&buffer->zdata, out, encode);
  if (tmp < 0) {
    return NULL;
  }

  /* Init event and add it to active events */
  /* Init event */
  event = (struct ev_io*) malloc(sizeof(struct ev_io));
  if (event == NULL) {
    PRINTF("Unable to use malloc\n")
    return NULL;
  }
  ev_io_init(event, read_cb, buffer->mon.fd, EV_READ);
  event->data = mon;
  ev_io_start(event_loop, event);
  return event;
}

/**
 * Create the libev io event that listen on the real interface and drop every packet.
 * @param port          Port to bind on
 * @param mreq          Multicast address to bind on (with scope)
 * @return started libev io event which listen for new packets
 */
static struct ev_io*
drop_on(in_port_t port, struct ipv6_mreq *mreq)
{
  int fd, tmp;
  struct ev_io* event;
  struct sockaddr_in6 ipaddr;

  /* Create socket */
  if ((fd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
    PERROR("socket")
    return NULL;
  }

  /* Bind Socket */
  memset(&ipaddr, 0, sizeof(struct sockaddr_in6));
  ipaddr.sin6_family = AF_INET6;
  ipaddr.sin6_port = htons(port);
  if (bind(fd, (struct sockaddr *)&ipaddr, sizeof(struct sockaddr_in6)) < 0) {
    PERROR("bind()")
    return NULL;
  }

  /* Also listen on Multicast */
  tmp = setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, mreq, sizeof(struct ipv6_mreq));
  if (tmp == -1) {
    PERROR("setsockopt(IPV6_JOIN_GROUP)")
    return NULL;
  }

  /* Init event and add it to active events */
  event = (struct ev_io*) malloc(sizeof(struct ev_io));
  if (event == NULL) {
    PRINTF("Unable to use malloc\n")
    return NULL;
  }
  ev_io_init(event, drop_cb, fd, EV_READ);
  ev_io_start(event_loop, event);
  return event;
}

/**
 * Callback of the greload event.
 * Change the active file for output.
 * Standard prototype for libev's ev_periodic callback.
 * @param loop     Event loop on which this event is.
 * @param periodic Event that tiggered this call, should be 'send_mess'.
 * @param revents  Causes of this call.
 */
static void
reload_cb(struct ev_loop *loop, ev_periodic *periodic, int revents)
{
  struct server_buffer* in;
  size_t len;
  FILE* dest;
  int tmp;

  /* Retrieve local data */
  in = (struct server_buffer*) periodic->data;
  assert(in != NULL);

  /* Basic verification */
  if(in->filename == NULL) {
    fprintf(stderr, "Unable to change the name of the output file: no filename specified");
    return;
  }
  ++in->filename_count;
  if (in->filename_count >= 1000) {
    fprintf(stderr, "Unable to change the name of the output file: count >= 1000\n");
    return;
  }

  /* Prepare the next filename */
  len = strlen(in->filename);
  assert(len > 7);
  snprintf(in->filename + (len - 6), 7, "%03i.gz", in->filename_count);

  /* Try to open the new file */
  dest = fopen(in->filename, "w");
  if (dest == NULL) {
    fprintf(stderr, "Unable to change the name of the output file: Unable to open output file (%s) : %i\n", in->filename, errno);
    return;
  }

  /* Swap zlib opened stream */
  zend_data(&in->zdata);
  memset(&in->zdata, 0, sizeof(struct zutil_write));
  tmp = zinit_write(&in->zdata, dest, in->encode);
  assert(tmp >= 0);
}

/**
 * Callback for signals 'SIGHUP'.
 * Rescedule reload_cb if needed/possible
 * @param sig Signal received (should be SIGHUP).
 */
static void
reload(int sig)
{
  if (greload == NULL) {
    printf("Feature not activated at program launch, not available now\n");
    return;
  }
  if (!(ev_is_active(greload) && ev_is_pending(greload))) {
    greload->offset = ev_now(event_loop) + 0.001;
    ev_periodic_again(event_loop, greload);
  } else {
    printf("Reload should be triggered now thus just wait\n");
  }
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
//! Default "file" output (not recommanded)
#define SERVER_DEFAULT_FILE stdout
//! Default listening port
#define SERVER_DEFAULT_PORT 10101
//! Default Zlib encoding level
#define SERVER_DEFAULT_ENCODE 7
//! Default multicast address to listen on
#define SERVER_DEFAULT_MULTICAST "ff02::1"
//! Default interface to listen on
#define SERVER_DEFAULT_INTERFACE "wlan0"
//! Disable data file rotating by default
#define SERVER_DEFAULT_RELOAD 0

/**
 * Print a short howto and exit.
 * @param err Execution code to return.
 * @param name Name under which this program was called.
 */
static void
usage(int err, char *name)
{
  printf("%s: Listen on a given socket and store timestamped packet content\n", name);
  printf("Usage: %s [OPTIONS]\n", name);
  printf("Options:\n");
  printf(" -h, --help           Print this ...\n");
  printf(" -o, --ouput  <file>  Specify the output file (default: standard output)\n");
  printf(" -r, --rand           Randomize the output file by adding a random number\n");
  printf("     --reload <secs>  Change the output file every <secs> seconds (disabled if <secs> <= 0, disabled by default)\n");
  printf("                       If enabled, also grant the user to manually rotate the file by sending a SIGHUP.");
  printf(" -l, --level  [0-9]   Specify the level of the output compression (default : %i)\n", SERVER_DEFAULT_ENCODE);
  printf(" -p, --port   <port>  Specify the port to listen on (default: %"PRIu16")\n", SERVER_DEFAULT_PORT);
  printf(" -b           <addr>  Specify the address used for multicast (default : %s)\n", SERVER_DEFAULT_MULTICAST);
  printf(" -i      <interface>  Specify the interface to bind on (default : %s)\n", SERVER_DEFAULT_INTERFACE);

  exit(err);
}

/**
 * Long options used by getopt_long; see 'usage' for more detail.
 */
static const struct option long_options[] = {
  {"help",              no_argument, 0,  'h' },
  {"rand",              no_argument, 0,  'r' },
  {"reload",      required_argument, 0,  'z' },
  {"output",      required_argument, 0,  'o' },
  {"level",       required_argument, 0,  'l' },
  {"port",        required_argument, 0,  'p' },
  {NULL,                          0, 0,   0  }
};

/**
 * Main function for the server tool.
 * @param argc Argument Count
 * @param argv Argument Vector
 * @return Execution return code
 */
int
main(int argc, char *argv[])
{
  int opt;
  int randi = 0;
  int encode = SERVER_DEFAULT_ENCODE;
  char *filename = NULL;
  char *filetemp;
  in_port_t port = SERVER_DEFAULT_PORT;
  FILE *dest = SERVER_DEFAULT_FILE;
  char *addr_s = NULL;
  const char *interface = NULL;
  uint8_t randomized;
  FILE *randsrc;
  struct in6_addr multicast;
  struct ipv6_mreq mreq;
  float  reload_timer = SERVER_DEFAULT_RELOAD;

  while((opt = getopt_long(argc, argv, "hro:p:b:i:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage(0, argv[0]);
        return 0;
      case 'r':
        randi = 1;
        break;
      case 'z':
        if (reload_timer != SERVER_DEFAULT_RELOAD) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%f", &reload_timer);
        break;
      case 'o':
        filename = optarg;
        break;
      case 'l':
        if (encode != SERVER_DEFAULT_ENCODE) {
          usage(1, argv[0]);
        }
        sscanf(optarg, "%i", &encode);
        if (encode < 0 || encode > 9) {
          usage(1, argv[0]);
        }
        break;
      case 'm':
        if (port != SERVER_DEFAULT_PORT) {
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
    *filetemp = '\0';
    if (randi) {
      filetemp = malloc(strlen(filename) + 12);
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
      snprintf(filetemp, strlen(filename) + 12, "%s-%"PRIu8".000.gz", filename, randomized);
      filename = filetemp;
    } else {
      filetemp = malloc(strlen(filename) + 8);
      assert(filetemp != NULL);
      snprintf(filetemp, strlen(filename) + 8, "%s.000.gz", filename);
      filename = filetemp;
    }
    dest = fopen(filename, "w");
    if (dest == NULL) {
      printf("Unable to open output file (%s)\n", filename);
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
     int temp = inet_pton(AF_INET6, SERVER_DEFAULT_MULTICAST, &multicast);
     assert(temp == 1);
  }

  memcpy(&mreq.ipv6mr_multiaddr, &multicast, sizeof(struct in6_addr));

  if (interface != NULL) {
    if ((mreq.ipv6mr_interface = if_nametoindex(interface)) == 0) {
      printf("Error, the given interface doesn't exist\n");
      return -1;
    }
  } else {
    if ((mreq.ipv6mr_interface = if_nametoindex(SERVER_DEFAULT_INTERFACE)) == 0) {
      printf("Error, the default interface doesn't exist, please specify one using -i\n");
      return -1;
    }
    interface = SERVER_DEFAULT_INTERFACE;
  }

  event_loop = ev_default_loop (EVFLAG_AUTO);

  if((event_killer = (ev_timer*) malloc(sizeof(ev_timer))) == NULL) {
    PRINTF("Malloc\n")
    return -1;
  }
  ev_init(event_killer, event_end);

  glisten = listen_on(port, "mon0", 0, interface, &multicast, dest, encode, filename);
  if (glisten == NULL) {
    PRINTF("Unable to create listening event (libevent)\n")
    return -2;
  }

  gdrop = drop_on(port, &mreq);
  if (gdrop == NULL) {
    PRINTF("Unable to create listening event (libevent)\n")
    return -2;
  }

  if (reload_timer > 0) {
    if((greload = (ev_periodic*) malloc(sizeof(ev_periodic))) == NULL) {
      PRINTF("Malloc\n")
      return -1;
    }
    ev_periodic_init(greload, reload_cb, ev_now(event_loop), reload_timer, NULL);
    greload->data = glisten->data;

    ev_periodic_start(event_loop, greload);
  } else {
    greload = NULL;
  }

  signal(SIGINT, down);
  signal(SIGABRT, down);
  signal(SIGQUIT, down);
  signal(SIGTERM, down);
  signal(SIGHUP, reload);

  ev_loop(event_loop, 0);

  struct server_buffer* arg;

  arg = glisten->data;
  zend_data(&arg->zdata);
  close_interface(arg->mon_name);
  close(glisten->fd);
  close(gdrop->fd);
  free(arg->filename);
  ev_io_stop(event_loop, glisten);
  free(glisten);
  ev_io_stop(event_loop, gdrop);
  free(gdrop);
  if (reload_timer > 0) {
    ev_periodic_stop(event_loop, greload);
    free(greload);
  }
  ev_timer_stop(event_loop, event_killer);
  free(event_killer);
  free(arg);
  ev_default_destroy();

  return 0;
}
