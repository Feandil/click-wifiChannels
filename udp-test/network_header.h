#ifndef NETWORK_HEADER_H
#define NETWORK_HEADER_H

struct header_80211 {
    u_int16_t    fc;          /* 2 bytes */
    u_int16_t    duration;    /* 2 bytes */
    u_int8_t     da[6];       /* 6 bytes */
    u_int8_t     sa[6];       /* 6 bytes */
    u_int8_t     bssid[6];    /* 6 bytes */
    u_int16_t    seq_ctrl;    /* 2 bytes */
};

struct header_llc {
  u_int8_t  dsap;
  u_int8_t  ssap;
  u_int8_t  controle;
  u_int8_t  org_code[3];
  u_int16_t type;
};

struct header_ipv6 {
   unsigned   traffic1 :  4;
   unsigned   version  :  4;
   unsigned   traffic2 :  4;
   unsigned   flow     : 20;
   u_int16_t  payload_length;
   u_int8_t   next;
   u_int8_t   hop;
   u_int16_t  src[8];
   u_int16_t  dst[8];
};

struct header_udp {
  u_int16_t src_port;
  u_int16_t dst_port;
  u_int16_t len;
  u_int16_t chksum;
};

#endif /* NETWORK_HEADER_H */
