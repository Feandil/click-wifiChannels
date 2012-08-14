#ifndef NETWORK_HEADER_H
#define NETWORK_HEADER_H

#include <inttypes.h>

/**
 * Wifi (802.11) header
 */
struct header_80211 {
    uint16_t    fc;          //< Frame control: 2 bytes
    uint16_t    duration;    //< Duration ID: 2 bytes
    uint8_t     da[6];       //< Address 1 (Destination): 6 bytes
    uint8_t     sa[6];       //< Address 2 (Source): 6 bytes
    uint8_t     bssid[6];    //< Address 2 (bssid): 6 bytes
    uint16_t    seq_ctrl;    //< Sequence Control: 2 bytes
};

/**
 * Logical Link Control (802.2) header with Subnetwork Access Procol (SNAP) extenstion
 */
struct header_llc {
  uint8_t  dsap;        //< Destination service access point
  uint8_t  ssap;        //< Source service access poinrt
  uint8_t  controle;    //< Control
  uint8_t  org_code[3]; //< IEEE Organizationally Unique Identifier
  uint16_t type;        //< Protocol ID
};

/**
 * IPv6 header
 */
struct header_ipv6 {
   unsigned   traffic1 :  4; //< First part of the traffic field (Endiannes problems)
   unsigned   version  :  4; //< IP version number (Should be 0x06 for IPv6)
   unsigned   traffic2 :  4; //< Second part of the traffic field (Endiannes problems)
   unsigned   flow     : 20; //< Flow control field
   uint16_t  payload_length; //< Payload length in octets
   uint8_t   next;           //< Type of the next header
   uint8_t   hop;            //< Hope Limit
   uint16_t  src[8];         //< Source IPv6 address
   uint16_t  dst[8];         //< Destination IPv6 address
};

/**
 *  UDP over IPv6 pseudo-header (Next header 17)
 */
struct header_udp {
  uint16_t src_port; //< Source port
  uint16_t dst_port; //< Destination port
  uint16_t len;      //< Length in bytes
  uint16_t chksum;   //< Checksum, including the IPv6 header
};

#endif /* NETWORK_HEADER_H */
