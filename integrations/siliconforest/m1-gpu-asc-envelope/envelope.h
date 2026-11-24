/* Original source; license selection pending. Detached bytes only. */
#ifndef ACE_ENVELOPE_H
#define ACE_ENVELOPE_H
#include <stddef.h>
#include <stdint.h>
#define ACE_BYTES 16u
/* Explicit source-layout selection, never a loaded-device/firmware assertion. */
enum ace_profile { ACE_PROFILE_UNKNOWN = 0, ACE_PIN121FCBF_LEGACY = 1 };
enum ace_code { ACE_OK, ACE_ARGUMENT, ACE_PROFILE, ACE_LENGTH, ACE_ENDPOINT,
                ACE_OVERLAP, ACE_METADATA };
struct ace_rx {
    uint64_t word, metadata, opaque, endpoint, outcnt, incnt, outptr, inptr;
};
struct ace_message { uint64_t word, endpoint; };
/* Caller supplies valid readable/writable objects and serializes their lifetime.
 * Exact 16-byte canonical LE serialization of detached word0,word1; not a bus
 * transaction or atomic capture. Input/output spans must be disjoint. Every
 * failure leaves output unchanged. No function grants transport acceptance. */
enum ace_code ace_decode_rx(enum ace_profile, const uint8_t *, size_t, struct ace_rx *);
/* Fresh endpoint-only metadata, not a replay of RX telemetry. */
enum ace_code ace_encode_tx(enum ace_profile, uint32_t endpoint, uint64_t word,
                            uint8_t *, size_t);
/* Conservative pinned C rtkit_recv low32<256 policy, separately from lossless
 * decoding. Upper32 remains opaque/telemetry; this is not a freshness check. */
enum ace_code ace_rtkit_input(enum ace_profile, const uint8_t *, size_t,
                              struct ace_message *);
#endif
