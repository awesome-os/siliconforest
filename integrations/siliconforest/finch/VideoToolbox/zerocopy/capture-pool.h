/* SPDX-License-Identifier: MIT */
/* A private measurement opt-in, never a promise that the codec minimum covers
 * a browser's outstanding consumer frames. The default remains the full pool.
 * Invalid/overflowing requests must not silently reduce that safe baseline. */
#ifndef SF_SFAVD_CAPTURE_POOL_H
#define SF_SFAVD_CAPTURE_POOL_H
static unsigned sf_sfavd_capture_pool(unsigned codec_minimum, unsigned maximum,
                                      const char *requested)
{
    if (!requested || !*requested) return maximum;
    unsigned count = 0;
    for (const unsigned char *p = (const unsigned char *)requested; *p; ++p) {
        if (*p < '0' || *p > '9') return maximum;
        unsigned digit = *p - '0';
        if (digit > maximum || count > (maximum - digit) / 10) return maximum;
        count = count * 10 + digit;
    }
    return count && count >= codec_minimum && count <= maximum ? count : maximum;
}
#endif
