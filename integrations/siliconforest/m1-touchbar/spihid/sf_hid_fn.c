/* SPDX-License-Identifier: MIT */
/* See sf_hid_fn.h. Item encoding: USB Device Class Definition for HID 1.11, section 6.2.2. */
#include "sf_hid_fn.h"

#define MAX_USAGES 16

int
sf_hid_input_field(const uint8_t *d, size_t n, unsigned report_id, unsigned want_page, unsigned want_usage, unsigned *width)
{
	unsigned page = 0, size = 0, count = 0, id = 0, ids = 0;
	unsigned usages[MAX_USAGES], nusages = 0, umin = 0, umax = 0, have_range = 0;
	unsigned bits[256];
	size_t i = 0;

	for (unsigned k = 0; k < 256; k++)
		bits[k] = 0;
	while (i < n) {
		uint8_t prefix = d[i++];
		unsigned len = prefix & 3, type = (prefix >> 2) & 3, tag = prefix >> 4;
		uint32_t value = 0;

		if (prefix == 0xfe) {           /* long item: skipped */
			if (i + 2 > n || i + 2 + d[i] > n)
				return -1;
			i += 2 + d[i];
			continue;
		}
		if (len == 3)
			len = 4;
		if (i + len > n)
			return -1;
		for (unsigned b = 0; b < len; b++)
			value |= (uint32_t)d[i + b] << (8 * b);
		i += len;

		if (type == 1) {                /* global */
			if (tag == 0)
				page = value;
			else if (tag == 7)
				size = value;
			else if (tag == 8) {
				id = value & 0xff;
				ids = 1;
			} else if (tag == 9)
				count = value;
		} else if (type == 2) {         /* local */
			/* a four-byte usage carries its page in the upper half */
			if (tag == 0 && nusages < MAX_USAGES)
				usages[nusages++] = len == 4 ? value : (page << 16 | (value & 0xffff));
			else if (tag == 1) {
				umin = len == 4 ? value : (page << 16 | (value & 0xffff));
				have_range = 1;
			} else if (tag == 2)
				umax = len == 4 ? value : (page << 16 | (value & 0xffff));
		} else if (type == 0) {         /* main */
			if (tag == 8) {         /* input */
				if (bits[id] == 0 && ids)
					bits[id] = 8;   /* the report ID byte */
				for (unsigned f = 0; f < count && id == report_id && !(value & 1); f++) {
					unsigned u;
					if (nusages != 0)
						u = usages[f < nusages ? f : nusages - 1];
					else if (have_range && umin + f <= umax)
						u = umin + f;
					else
						break;
					/* a usage written with a two-byte page item (0xff00...) is taken by its low page byte too */
					if (size >= 1 && size <= 32 && (value & 2) && (u & 0xffff) == want_usage &&
					    ((u >> 16) == want_page || ((u >> 16) & 0xff00) == (want_page << 8))) {
						if (width != NULL)
							*width = size;
						return (int)(bits[id] + f * size);
					}
				}
				if (size != 0 && count > (0x10000u / size))
					return -1;
				bits[id] += size * count;
			}
			nusages = 0;            /* local items end with every main item */
			have_range = 0;
			umin = umax = 0;
		}
	}
	return -1;
}
