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
#ifdef __cplusplus
}
#endif
#endif
