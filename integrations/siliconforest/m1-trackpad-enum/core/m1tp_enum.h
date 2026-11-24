/* Original trackpad enumeration helpers. License pending.
 * Not a transport. Claim a key before the caller increments an ID, encodes,
 * or sends. Matching stays m1kbd_response_matches. This is not a lock.
 */
#ifndef M1TP_ENUM_H
#define M1TP_ENUM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#define M1TP_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define M1TP_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

#define M1TP_OK 0
#define M1TP_ERR_ARGUMENT (-1)
#define M1TP_ERR_LENGTH (-2)
#define M1TP_ERR_INTERFACE (-3)
#define M1TP_ERR_INPUT (-4)

#define M1TP_CLAIM_MASK 31u
#define M1TP_BIT_INFO 1u
#define M1TP_BIT_IFACE1 2u
#define M1TP_BIT_DESC1 4u
#define M1TP_BIT_IFACE2 8u
#define M1TP_BIT_DESC2 16u
#define M1TP_META_MIN 16u
#define M1TP_META_MAX 2038u
#define M1TP_INPUT_MIN 1u
#define M1TP_INPUT_MAX 65535u
#define M1TP_DESC_MIN 1u
#define M1TP_DESC_MAX 512u

/* 8 bytes, no padding: three little-endian limits, then interface and country. */
struct m1tp_interface_info {
    uint16_t max_input;
    uint16_t max_output;
    uint16_t max_control;
    uint8_t interface;
    uint8_t country;
};

M1TP_ASSERT(M1TP_CLAIM_MASK == 31u, "five one-use keys");
M1TP_ASSERT((M1TP_BIT_INFO | M1TP_BIT_IFACE1 | M1TP_BIT_DESC1 | M1TP_BIT_IFACE2 |
             M1TP_BIT_DESC2) == M1TP_CLAIM_MASK, "claim bits");
M1TP_ASSERT(M1TP_META_MIN == 16u, "numeric header");
M1TP_ASSERT(M1TP_META_MAX == 2048u - 10u, "payload cap");
M1TP_ASSERT(M1TP_INPUT_MIN == 1u && M1TP_INPUT_MAX == 65535u, "nonzero advertised input");
M1TP_ASSERT(M1TP_DESC_MIN == 1u && M1TP_DESC_MAX == 512u, "descriptor cap");
M1TP_ASSERT(sizeof(struct m1tp_interface_info) == 8u, "iface record");
M1TP_ASSERT(offsetof(struct m1tp_interface_info, max_input) == 0u, "input");
M1TP_ASSERT(offsetof(struct m1tp_interface_info, max_output) == 2u, "output");
M1TP_ASSERT(offsetof(struct m1tp_interface_info, max_control) == 4u, "control");
M1TP_ASSERT(offsetof(struct m1tp_interface_info, interface) == 6u, "interface");
M1TP_ASSERT(offsetof(struct m1tp_interface_info, country) == 7u, "country");

/* 1 if this key is claimed, 0 if refused. Refusal leaves *used unchanged.
 * Bits: 01/d0, 02/01, 10/01, 02/02, 10/02. A mask outside 31 is refused.
 * Zero-initialize once. A used key is never cleared here. */
int m1tp_claim_request(uint8_t *used, uint8_t report, uint8_t subdevice);

/* Interface 2 numeric header only. n is 16..2038. Byte 1 is 2. Country is
 * byte 5. Input at offset 6 is any nonzero uint16 and is stored as a
 * diagnostic, not a buffer or parser size. Zero is refused. Output at 8 and
 * control at 10 are copied for any uint16 value. Bytes after the header,
 * including a name span, are not read. Failure leaves *out unchanged.
 * 0 or a negative error. */
int m1tp_interface_info(const uint8_t *payload, size_t n,
                        struct m1tp_interface_info *out);

/* Copy n bytes, 1..512 and n <= cap, into caller storage. Failure leaves
 * dst and *length unchanged. The bytes are not parsed. 0 or a negative error. */
int m1tp_copy_descriptor(uint8_t *dst, size_t cap, uint32_t *length,
                         const uint8_t *src, size_t n);

#ifdef __cplusplus
}
#endif
#endif
