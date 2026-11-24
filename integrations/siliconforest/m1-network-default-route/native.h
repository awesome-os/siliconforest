#ifndef DLF_NATIVE_H
#define DLF_NATIVE_H
#include "default_route.h"
#include "owned/owned_interfaces.h"
#include "inventory.h"
struct dlf_native {
    struct dlf_session lifetime;
    struct dle_interfaces owned;
    void (*record)(const char *,const uint8_t *,size_t,void *);
    void *record_context;
    int last_errno;
    unsigned baseline_routes_known;
    struct dlf_route_set baseline_routes,final_routes,scratch_routes;
    uint8_t dump[DLF_DUMP_LIMIT];
};
/* Fresh zeroed, noncopied caller storage; no concurrent networking configurator.
 * No observer callback may reenter this context. Foreground/signal-safe owner
 * must call close on every error. Allocate this bounded context on the heap.
 * This is not a kernel-wide configuration lease.
 */
int dlf_native_init(struct dlf_native *,int exclusive,
    void (*record)(const char *,const uint8_t *,size_t,void *),void *);
int dlf_native_open(struct dlf_native *);
int dlf_native_close(struct dlf_native *);
/* Descriptor is available only after actual ACK and independent route GET.
 * Caller owns no fd: it must not close, dup, pass or reconfigure this descriptor.
 */
int dlf_native_fd(const struct dlf_native *);
#endif
