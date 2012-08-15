/*
 * crc32.h
 */
#ifndef _CRC32_H
#define _CRC32_H

#include <inttypes.h>
#include <stdlib.h>

/**
 * Evaluate the CRC32 (802.11 version) of the input
 * @param p Pointer to the input memory zone
 * @param len Size of the input memory zone
 * @return CRC32 of the input memory zone, should be 0 for a well formed 802.11 message
*/
uint32_t __attribute__((pure)) crc32_80211(const uint8_t *p, size_t len);

#endif /* _CRC32_H */
