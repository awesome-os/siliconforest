#ifndef M1KBD_HID_H
#define M1KBD_HID_H
#include "native_protocol/dl_spi_hid.h"
#ifdef __cplusplus
extern "C" {
#endif
struct m1kbd_identity {
    uint16_t vendor, product, version, max_input;
    uint8_t country;
};
/* Called only on a CRC-validated, request-identity-matched message.
 * Failure leaves output unchanged. Metadata string offsets are checked,
 * but strings/serials are not copied or published. */
int m1kbd_device_info(const uint8_t *, size_t, struct m1kbd_identity *);
int m1kbd_interface_info(const uint8_t *, size_t, struct m1kbd_identity *);
int m1kbd_response_matches(const struct dlsp_message *, uint8_t report, uint8_t subdevice, uint8_t id);
/* 1: eligible device-1 input, 0: another message, -1: malformed keyboard input.
 * IOHID still validates the actual descriptor/report relationship. */
int m1kbd_keyboard_input(const struct dlsp_message *, uint16_t max_input);
/* Diagnostic schema v1: fixed u32 fields only, no packet/message payload or
 * borrowed pointer. Outer fields can be untrusted when parser_error != 0.
 * Supply message only after DLSP_MESSAGE (both CRCs/lengths checked). */
struct m1kbd_observation {
    uint32_t packet_available, packet_flags, packet_device, offset, remain, length;
    uint32_t message_available, device, flags, command, report, subdevice, id;
    uint32_t response_length, payload_length, parser_error;
};
void m1kbd_observe(const uint8_t *, size_t, const struct dlsp_message *,
                  unsigned parser_error, struct m1kbd_observation *);
#ifdef __cplusplus
}
#endif
#endif
