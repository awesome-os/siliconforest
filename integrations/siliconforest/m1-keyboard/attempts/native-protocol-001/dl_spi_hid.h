/* Original bounded wire engine. Facts: pinned spi-hid-apple-core.c at
 * linux-aurora 6a08a08. No Linux implementation is copied. License pending. */
#ifndef DL_SPI_HID_H
#define DL_SPI_HID_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DLSP_PACKET_BYTES 256u
#define DLSP_FRAGMENT_BYTES 246u
#define DLSP_MESSAGE_BYTES 2048u
#define DLSP_DESCRIPTOR_BYTES 512u
enum dlsp_result { DLSP_INVALID=-1, DLSP_WAIT=0, DLSP_MESSAGE=1, DLSP_BOOT=2 };
enum dlsp_error { DLSP_E_NONE, DLSP_E_ARGUMENT, DLSP_E_PACKET_CRC, DLSP_E_FLAGS,
    DLSP_E_DEVICE, DLSP_E_LENGTH, DLSP_E_ORDER, DLSP_E_MESSAGE_CRC, DLSP_E_DESCRIPTOR };
struct dlsp_message {
    uint8_t device, flags, command, report, subdevice, id;
    uint16_t response_length;
    const uint8_t *payload;
    size_t payload_length;
};
struct dlsp_state {
    uint8_t assembling[DLSP_MESSAGE_BYTES], delivered[DLSP_MESSAGE_BYTES];
    size_t used, total;
    unsigned active;
    uint8_t device, flags;
    enum dlsp_error last_error;
};
/* One serialized caller. Storage belongs to the caller (heap in the kext).
 * Output payload is borrowed until the next push/reset. Invalid input emits
 * no message, clears partial assembly and sets last_error. Single-packet
 * messages may interleave with a fragmented response without destroying it.
 * BOOT is the exact CRC-valid four-byte a0 80 00 00 notification; like the
 * reference, its outer flags/device/offset/remain are not interpreted. */
void dlsp_init(struct dlsp_state *);
uint16_t dlsp_crc16(const uint8_t *, size_t);
int dlsp_push(struct dlsp_state *, const uint8_t *, size_t, struct dlsp_message *);
/* Build one write packet, including both CRCs. Maximum request payload 236.
 * Failure leaves output untouched. Descriptor request/response is capped at
 * 512 bytes; a larger generic input message is still capped at 2048 total.
 * The four-byte SPI write-status response belongs to the transport layer. */
int dlsp_request(uint8_t device, uint8_t command, uint8_t report,
    uint8_t subdevice, uint8_t id, uint16_t response_length,
    const uint8_t *payload, size_t payload_length, uint8_t output[DLSP_PACKET_BYTES]);
#ifdef __cplusplus
}
#endif
#endif
