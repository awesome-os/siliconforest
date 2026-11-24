/* Original fake-only shared Wi-Fi/Bluetooth enable command slice.
 * MIT reference facts and unknowns: RADIO_ENABLE.md. */
#ifndef DL_APCIE_RADIO_ENABLE_H
#define DL_APCIE_RADIO_ENABLE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define ARS_SMC_GPIO_PIN 13u
#define ARS_ENABLE_COMMAND UINT32_C(0x00800001)

enum ars_result {
    ARS_FAKE_ACCEPTED = 0, /* fake callback accepted command; not radio-state proof */
    ARS_GATED = 1, ARS_ARGUMENT = -1, ARS_IO = -2
};

/* Only the existing host process's fake SMC GPIO file may implement this
 * interface. key is exactly four ASCII bytes (no terminating NUL); value is
 * exactly length bytes, little-endian command data. Callback returns 0 only
 * when all length bytes were accepted, otherwise nonzero. No transport, SMC
 * endpoint, physical address, device, power or clock implementation is supplied.
 * Context must remain stable; caller serializes and prevents callback reentry. */
struct ars_io {
    void *context;
    int (*write_key)(void *, const char key[4], const uint8_t *value, uint32_t length);
};

/* One call's command bookkeeping. state_known is ALWAYS 0, including after
 * a successful fake write. It makes no live read or claim of current power.
 * dirty is set before attempting the write: error may follow an applied
 * command. No retry, rollback, disable, power-cycle or implicit delay exists. */
struct ars_snapshot {
    uint32_t version;
    int32_t result;
    uint32_t pin, requested_command, writes_attempted, command_accepted;
    uint32_t dirty, gated, state_known;
};

/* 1 only under ARS_FAKE_GPIO_FILE, which a KERNEL build refuses. */
int ars_fake_enable_compiled(void);

/* Fixed gP0d enable command only. No caller-selectable pin/value or off path.
 * Default/KERNEL returns ARS_GATED without invoking any callback.
 * Snapshot must not alias io or its context. Neither ARS_FAKE_ACCEPTED nor
 * command_accepted certifies line level, Wi-Fi-only power, settle or ownership. */
int ars_enable_radio(const struct ars_io *io, struct ars_snapshot *snapshot);

#ifdef __cplusplus
}
#endif
#endif
