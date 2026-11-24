/* SPDX-License-Identifier: MIT */
/* Private ICC profile checksum, independent of the platform CC digest state. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sf_md5.h"

static void sf_profile_md5(const void *bytes, size_t length, uint8_t digest[16])
{
    struct sf_md5 state;
    char hex[33];
    sf_md5_init(&state);
    sf_md5_update(&state, bytes, length);
    sf_md5_final(&state, hex);
    for (unsigned i = 0; i < 16; i++) {
        unsigned high = hex[2*i] <= '9' ? hex[2*i]-'0' : hex[2*i]-'a'+10;
        unsigned low = hex[2*i+1] <= '9' ? hex[2*i+1]-'0' : hex[2*i+1]-'a'+10;
        digest[i] = (uint8_t)((high << 4) | low);
    }
}
