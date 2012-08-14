#ifndef RADIOTAP_PARSER_H
#define RADIOTAP_PARSER_H

#include <stdint.h>
#include <sys/types.h>
#include "ieee80211_radiotap.h"

/**
 *  Internal iterator for radiotap exploration.
 */
struct ieee80211_radiotap_iterator {
	struct ieee80211_radiotap_header *hdr; //< Base header.
        uint8_t *next;                         //< Next argument position.
	uint8_t *arg;                          //< Current argument position.

	ssize_t len;                           //< Length of the header
        uint32_t bitmap;                       //< Bitmap of the argument contained in the header
	unsigned int index;                    //< Next index
};

/**
 * Initialize radiotap header parsing.
 * @param iterator        Zeroed memory for storing internal information.
 * @param radiotap_header Input data memory zone pointer.
 * @param len             Input data memory zone size.
 * @return                Error code: 0 if OK, -EINVAL in case of failure.
 */
int ieee80211_radiotap_iterator_init(struct ieee80211_radiotap_iterator * iterator, struct ieee80211_radiotap_header * radiotap_header, ssize_t len);

/**
 * Fetch next content of the radiotap header with limited exploration
 * @param iterator  Internal information, need to be initialized with ieee80211_radiotap_iterator_init
 * @param max_index Index at which the exploration should stop
 * @return If >= 0, index of the argument present at iterator->arg; if -1, EOF; if -EINVAL, failure
 */
int ieee80211_radiotap_iterator_next(struct ieee80211_radiotap_iterator *iterator, uint8_t max_index);

#endif /* RADIOTAP_PARSER_H */


