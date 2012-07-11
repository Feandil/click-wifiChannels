#ifndef RADIOTAP_PARSER_H
#define RADIOTAP_PARSER_H

#include <stdint.h>
#include <sys/types.h>
#include "ieee80211_radiotap.h"

struct ieee80211_radiotap_iterator {
	struct ieee80211_radiotap_header *hdr;
        uint8_t *next;
	uint8_t *arg;

	ssize_t len;
        uint32_t bitmap;
	uint8_t index;
};

int ieee80211_radiotap_iterator_init(struct ieee80211_radiotap_iterator * iterator, struct ieee80211_radiotap_header * radiotap_header, ssize_t len);
int ieee80211_radiotap_iterator_next(struct ieee80211_radiotap_iterator *iterator, uint8_t max_index);

#endif /* RADIOTAP_PARSER_H */


