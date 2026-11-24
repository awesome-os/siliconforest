/* Original Darwin-lab adapter; licensing of original code is pending.
 * Register/protocol provenance and execution limits: ADAPTER.md. */
#ifndef M1KBD_HW_H
#define M1KBD_HW_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum m1kbd_result {
    M1KBD_OK = 0, M1KBD_NOT_READY = 1, M1KBD_INVALID = -1, M1KBD_BOARD = -2,
    M1KBD_IO = -3, M1KBD_TIMEOUT = -4, M1KBD_FIFO = -5,
    M1KBD_STATUS = -6, M1KBD_LOCKED = -7, M1KBD_STATE = -8,
    M1KBD_POWER = -9
};
enum m1kbd_region { M1KBD_SPI, M1KBD_GPIO, M1KBD_NUB_GPIO,
                     M1KBD_PMGR, M1KBD_NUB_PMGR, M1KBD_REGIONS };
#define M1KBD_PACKET_SIZE 256u
#define M1KBD_TRANSACTION_US 200000u
#define M1KBD_POWER_DOMAINS 6u

/* Return 0 and borrowed property bytes on success, nonzero on missing/error.
 * The property view must remain stable for the complete synchronous validation.
 * Native adapter resolves gIODTPlane, not Linux FDT phandles. */
typedef int (*m1kbd_property_fn)(void *, const char *, const char *,
                               const uint8_t **, size_t *);
struct m1kbd_board {
    uint64_t base[M1KBD_REGIONS];
    uint32_t size[M1KBD_REGIONS];
    uint32_t clock_hz, speed_hz;
    uint32_t qualified;
};
int m1kbd_validate_board(m1kbd_property_fn, void *, struct m1kbd_board *);

/* Caller owns mappings and serializes ALL functions on one worker/workloop.
 * Reads/writes are ordered 32-bit MMIO, not relaxed ordinary RAM accesses.
 * Region/offset checked below before dispatch. No callback may recurse here.
 * now_us is a monotonic clock; delay_us waits at least the requested duration.
 * Callbacks must be bounded themselves: software cannot recover a hung MMIO.
 * No DMA, allocation, interrupt handler or host/device I/O is hidden here. */
struct m1kbd_io {
    void *context;
    int (*read32)(void *, unsigned region, uint32_t offset, uint32_t *);
    int (*write32)(void *, unsigned region, uint32_t offset, uint32_t value);
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *, uint32_t);
};
struct m1kbd_hw {
    struct m1kbd_io io;
    struct m1kbd_board board;
    unsigned initialized, spi_access, gpio_access, ready, busy, faulted;
    unsigned cleanup_failed;
    uint32_t power_before[M1KBD_POWER_DOMAINS];
    uint32_t gpio_before[6]; /* AP46..49, AP195, NUB13, including LOCK/GRP */
};
int m1kbd_hw_init(struct m1kbd_hw *, const struct m1kbd_board *, const struct m1kbd_io *);
/* Verify firmware-owned/always-on domains; activate SPI_P/GPIO/SPI3 if needed.
 * Never reset or power down a parent. Configure
 * pins and controller, then keyboard enable 1/0/1 with 5/5/50 ms waits.
 * A completed start is not HID boot/enumeration or keyboard input proof. */
int m1kbd_hw_start(struct m1kbd_hw *);
int m1kbd_hw_ready(struct m1kbd_hw *, int *asserted);
int m1kbd_hw_read_packet(struct m1kbd_hw *, uint8_t packet[M1KBD_PACKET_SIZE]);
int m1kbd_hw_write_packet(struct m1kbd_hw *, const uint8_t packet[M1KBD_PACKET_SIZE]);
/* Stop/join the caller's callbacks before stop/unmap. Shared PMGR is never
 * restored, reset or powered down. Failed cleanup is reported, not success. */
int m1kbd_hw_stop(struct m1kbd_hw *);

#ifdef __cplusplus
}
#endif
#endif
