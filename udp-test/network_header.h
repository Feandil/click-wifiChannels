#ifndef NETWORK_HEADER_H
#define NETWORK_HEADER_H

#include <inttypes.h>

struct header_80211 {
    uint16_t    fc;          /* 2 bytes */
    uint16_t    duration;    /* 2 bytes */
    uint8_t     da[6];       /* 6 bytes */
    uint8_t     sa[6];       /* 6 bytes */
    uint8_t     bssid[6];    /* 6 bytes */
    uint16_t    seq_ctrl;    /* 2 bytes */
};

struct header_llc {
  uint8_t  dsap;
  uint8_t  ssap;
  uint8_t  controle;
  uint8_t  org_code[3];
  uint16_t type;
};

struct header_ipv6 {
   unsigned   traffic1 :  4;
   unsigned   version  :  4;
   unsigned   traffic2 :  4;
   unsigned   flow     : 20;
   uint16_t  payload_length;
   uint8_t   next;
   uint8_t   hop;
   uint16_t  src[8];
   uint16_t  dst[8];
};

struct header_udp {
  uint16_t src_port;
  uint16_t dst_port;
  uint16_t len;
  uint16_t chksum;
};

#endif /* NETWORK_HEADER_H */
