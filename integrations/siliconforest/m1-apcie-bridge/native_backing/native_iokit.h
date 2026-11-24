#ifndef BCM_NATIVE_IOKIT_H
#define BCM_NATIVE_IOKIT_H
#include "backing.h"
#ifdef __cplusplus
extern "C" {
#endif
struct bpn_native_setup {
    uint64_t identity, generation;
    struct bpl_range typed_anchor_cpu;
    uint64_t opaque_mutex_identity; /* address identity only; no size claim */
};
/* Single-threaded trusted boot setup BEFORE clients. Pin empty control objects
 * first; query only their identity and exact typed anchor CPU span. No backing
 * descriptors exist yet. Platform independently derives complete protected
 * spans. Configure binds them to the exact setup identity once. Invalid pure
 * configuration can be corrected while the manager is still zero; no backing
 * retry/reset is involved. Opaque IOLock allocation extent remains native
 * allocator/platform provenance, not sizeof/a guessed page span here. */
int bpn_native_pin_empty(struct bpn_native_setup *);
int bpn_native_configure(const struct bpn_native_setup *, const struct bpl_policy *, const struct bpn_protection *);
int bpn_native_acquire(const struct bpl_request *, uint64_t, uint32_t, struct bpn_token *);
int bpn_native_zero(const struct bpn_token *);
int bpn_native_copy(const struct bpn_token *, uint64_t, const void *, size_t);
int bpn_native_retire(const struct bpn_token *);
int bpn_native_inspect(uint32_t, struct bpn_snapshot *);
/* No descriptor/view export, destruction, unload, activation or force free. */
#ifdef __cplusplus
}
#endif
#endif
