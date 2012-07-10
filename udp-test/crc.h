/*
 * crc32.h
 */
#ifndef _CRC32_H
#define _CRC32_H

#include <inttypes.h>
#include <stdlib.h>

uint32_t crc32_80211(const uint8_t *p, size_t len);

#endif /* _CRC32_H */
