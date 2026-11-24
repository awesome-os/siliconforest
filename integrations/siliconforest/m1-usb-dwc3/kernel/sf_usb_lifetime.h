/* SPDX-License-Identifier: MIT */
/* Admission for controller operations while a cable session is being removed.
 * Closing prevents new operations; the owner waits for active==0 before
 * halting/resetting hardware. This does not itself establish DMA quiescence.
 */
#ifndef SF_USB_LIFETIME_H
#define SF_USB_LIFETIME_H
#include <stdint.h>
#define SF_USB_CLOSED (1u << 31)
struct sf_usb_lifetime { uint32_t state; };
static inline void sf_usb_lifetime_init(struct sf_usb_lifetime *g)
{ __atomic_store_n(&g->state, 0, __ATOMIC_RELEASE); }
static inline int sf_usb_lifetime_enter(struct sf_usb_lifetime *g)
{
    uint32_t old = __atomic_load_n(&g->state, __ATOMIC_ACQUIRE);
    for (;;) {
        if (old & SF_USB_CLOSED || old == SF_USB_CLOSED - 1) return 0;
        if (__atomic_compare_exchange_n(&g->state, &old, old + 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return 1;
    }
}
static inline void sf_usb_lifetime_leave(struct sf_usb_lifetime *g)
{ __atomic_fetch_sub(&g->state, 1, __ATOMIC_RELEASE); }
static inline void sf_usb_lifetime_close(struct sf_usb_lifetime *g)
{ __atomic_fetch_or(&g->state, SF_USB_CLOSED, __ATOMIC_ACQ_REL); }
static inline uint32_t sf_usb_lifetime_active(const struct sf_usb_lifetime *g)
{ return __atomic_load_n(&g->state, __ATOMIC_ACQUIRE) & ~SF_USB_CLOSED; }
static inline int sf_usb_lifetime_closed(const struct sf_usb_lifetime *g)
{ return !!(__atomic_load_n(&g->state, __ATOMIC_ACQUIRE) & SF_USB_CLOSED); }
#ifdef __cplusplus
class SFUSBOperation {
    struct sf_usb_lifetime *gate;
public:
    explicit SFUSBOperation(struct sf_usb_lifetime *g) : gate(sf_usb_lifetime_enter(g) ? g : nullptr) {}
    ~SFUSBOperation() { if (gate) sf_usb_lifetime_leave(gate); }
    explicit operator bool() const { return gate != nullptr; }
    SFUSBOperation(const SFUSBOperation &) = delete;
    SFUSBOperation &operator=(const SFUSBOperation &) = delete;
};
#endif
#endif
