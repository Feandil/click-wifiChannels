/*
 *  Copyright (c) 2007-2009         Andy Green <andy@warmcat.com>
 * Copyright (c) 2007-2009         Johannes Berg <johannes@sipsolutions.net>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
/*
 * Modifications :
 *  - Use standard asm/byteorder.h
 *  - Simplify (remove support of extend_mask)
 *  - No void* pointer arithmetic
 *  - Doxygen compatible documentation
 * Vincent Brillault (git@lerya.net)
 */
#include <asm/byteorder.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>

/** @file radiotap-parser.c Implementation of a parser for version 0 radiotap headers */

#include "radiotap-parser.h"

/*
 * Standard GNU "unlikely"
 */
#ifndef unlikely
# if defined(__GNUC__) && (__GNUC__ > 2) && defined(__OPTIMIZE__)
#  define unlikely(x)    __builtin_expect((x), 0)
# else
#  define unlikely(x)    (x)
# endif
#endif

/**
 * Initialize radiotap header parsing.
 * @param iterator        Zeroed memory for storing internal information.
 * @param radiotap_header Input data memory zone pointer.
 * @param len             Input data memory zone size.
 * @return                Error code: 0 if OK, -EINVAL in case of failure.
 */
int
ieee80211_radiotap_iterator_init(struct ieee80211_radiotap_iterator *iterator, struct ieee80211_radiotap_header *radiotap_header, ssize_t len)
{
  assert(iterator != NULL);
  assert(radiotap_header != NULL);
  /* We do not support versions != 0 */
  if (radiotap_header->it_version != 0) {
    return -EINVAL;
  }
  /* Check if we the radiotap header is complete */
  if (unlikely(len < __le16_to_cpu(radiotap_header->it_len))) {
    return -EINVAL;
  }
  iterator->bitmap = __le32_to_cpu(radiotap_header->it_present);
  /* We do not support the extension mask */
  if (unlikely((iterator->bitmap & IEEE80211_RADIOTAP_PRESENT_EXTEND_MASK) != 0)) {
    return -EINVAL;
  }
  iterator->hdr = radiotap_header;
  iterator->len = __le16_to_cpu(radiotap_header->it_len);
  iterator->next = ((uint8_t *)radiotap_header) +  sizeof (struct ieee80211_radiotap_header);
  iterator->arg = NULL;
  iterator->index = 0;
  return 0;
}

/**
 * Fetch next content of the radiotap header with limited exploration
 * @param iterator  Internal information, need to be initialized with ieee80211_radiotap_iterator_init
 * @param max_index Index at which the exploration should stop
 * @return If >= 0, index of the argument present at iterator->arg; if -1, EOF; if -EINVAL, failure
 */
int
ieee80211_radiotap_iterator_next(struct ieee80211_radiotap_iterator *iterator, uint8_t max_index)
{
  unsigned int index = 0;
  /*
   * small length lookup table for all radiotap types we heard of
   * starting from b0 in the bitmap, so we can walk the payload
   * area of the radiotap header
   *
   * upper nybble: content alignment for arg
   * lower nybble: content length for arg
  */
  static const uint8_t rt_sizes[] = {
    [IEEE80211_RADIOTAP_TSFT] = 0x88,
    [IEEE80211_RADIOTAP_FLAGS] = 0x11,
    [IEEE80211_RADIOTAP_RATE] = 0x11,
    [IEEE80211_RADIOTAP_CHANNEL] = 0x24,
    [IEEE80211_RADIOTAP_FHSS] = 0x22,
    [IEEE80211_RADIOTAP_DBM_ANTSIGNAL] = 0x11,
    [IEEE80211_RADIOTAP_DBM_ANTNOISE] = 0x11,
    [IEEE80211_RADIOTAP_LOCK_QUALITY] = 0x22,
    [IEEE80211_RADIOTAP_TX_ATTENUATION] = 0x22,
    [IEEE80211_RADIOTAP_DB_TX_ATTENUATION] = 0x22,
    [IEEE80211_RADIOTAP_DBM_TX_POWER] = 0x11,
    [IEEE80211_RADIOTAP_ANTENNA] = 0x11,
    [IEEE80211_RADIOTAP_DB_ANTSIGNAL] = 0x11,
    [IEEE80211_RADIOTAP_DB_ANTNOISE] = 0x11,
    [IEEE80211_RADIOTAP_RX_FLAGS] = 0x22,
    [IEEE80211_RADIOTAP_TX_FLAGS] = 0x22,
    [IEEE80211_RADIOTAP_RTS_RETRIES] = 0x11,
    [IEEE80211_RADIOTAP_DATA_RETRIES] = 0x11,
  };

  assert(max_index < sizeof (rt_sizes));

  /* The fields a required to be ordered, thus let's stop when we find what we searched */
  while (iterator->index <= max_index) {
    if (!(iterator->bitmap & 0x01)) {
      goto next_entry;
    }
    /* Padding nightmare */
    if ((((uint8_t*)iterator->next) - ((uint8_t*)iterator->hdr)) & ((rt_sizes[iterator->index] >> 4) - 1)) {
      iterator->arg = iterator->next + (rt_sizes[iterator->index] >> 4) - ((((uint8_t*)iterator->next) - ((uint8_t*)iterator->hdr)) & ((rt_sizes[iterator->index] >> 4) - 1));
    } else {
     iterator->arg = iterator->next;
    }
    index = iterator->index + 1;
    iterator->next = iterator->arg + (rt_sizes[iterator->index] & 0x0f);
    if ((((uint8_t*)iterator->next) - ((uint8_t*)iterator->hdr)) > iterator->len) {
      return -EINVAL;
    }
next_entry:
    ++iterator->index;
    iterator->bitmap >>= 1;
    if (index != 0) {
      /* index is bound by uint8_max + 1 thus cannot overflow int */
      return (int)(index - 1);
    }
  }
  return -1;
}
