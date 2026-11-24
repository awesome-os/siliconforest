/* Original serialized host registry. No native mapper/IRQ ownership claim. */
#ifndef DL_BCM4378_FAKE_OWNERSHIP_H
#define DL_BCM4378_FAKE_OWNERSHIP_H
#include "bcm4378_fake_dart.h"
#include "bcm4378_fake_irq.h"
#ifdef __cplusplus
extern "C" {
#endif

enum bfo_result {
    BFO_FAKE_OK = 0, BFO_GATED = 100, BFO_PHY_NOT_READY = 200,
    BFO_LINK_DOWN = 201, BFO_BUSY = 202,
    BFO_FAULTED = -300, BFO_OWNER = -301, BFO_ARGUMENT = -302,
    BFO_STATE = -303, BFO_IO = -304
}; /* Underlying BFD/BMI/DLD errors remain exact, never converted to success. */
struct bfo_mapping;
struct bfo_interrupt;
struct bfo_registry {
    struct apb_io io;
    struct bfo_mapping *stream; /* one exact RID0x100 binding, SID0..15 */
    struct bfo_interrupt *vectors[8];
    uint32_t bound, closed;
};
struct bfo_mapping {
    struct bfo_registry *registry;
    struct bfd_map map;
    uint32_t claimed, closed;
};
struct bfo_interrupt {
    struct bfo_registry *registry;
    struct bmi_source source;
    uint32_t vector, claimed, closed;
};
struct bfo_workspace {
    struct bfd_workspace codec;
    uint8_t root[DLD_PAGE], leaf[DLD_PAGE]; /* private inactive staging */
};

/* One canonical registry per fake endpoint; all users cooperate through it.
 * Caller serializes every call, including old BFD/BMI APIs, and must not bypass
 * this registry, copy/move owners, mutate their fields or create a second
 * registry for the same fake device. No global lock or hardware ownership.
 * All objects, policy arrays, outputs and table/workspace buffers must remain
 * live and disjoint. Zero-initialize each new object; closed objects cannot
 * reopen. No allocations or callbacks outliving a synchronous call. */
int bfo_registry_open(struct bfo_registry *, const struct apb_io *checked_fake);
int bfo_registry_close(struct bfo_registry *); /* empty registry only */
int bfo_map_open(struct bfo_registry *, struct bfo_mapping *,
                 const struct dld_policy *, const struct bfd_storage *, uint32_t sid);
int bfo_map_pages(struct bfo_mapping *, struct bfo_workspace *, uint64_t iova,
                  uint64_t pa, uint64_t bytes, unsigned permissions);
int bfo_translate(struct bfo_mapping *, struct bfo_workspace *, uint32_t rid, uint32_t sid,
                  uint64_t iova, unsigned access, uint64_t *pa);
/* Healthy software close/unmap clears owned fake table bytes and image, then
 * releases the binding. Backing remains caller-owned, never freed. Faulted
 * mappings refuse close/retry and retain the claim/backing until fixture
 * destruction. This is NOT a DART flush, DMA/IRQ drain or physical safe-free. */
int bfo_map_close(struct bfo_mapping *);
int bfo_irq_open(struct bfo_registry *, struct bfo_interrupt *, uint32_t vector);
int bfo_irq_mask(struct bfo_interrupt *, int masked);
int bfo_irq_receive(struct bfo_interrupt *, uint64_t address, uint32_t data);
int bfo_irq_dispatch(struct bfo_interrupt *, uint32_t *vector, uint32_t *aic_irq);
int bfo_irq_close(struct bfo_interrupt *); /* healthy, all masked, no pending */

/* Default/KERNEL: every operation GATED, no I/O or object/output changes.
 * Working host mode requires BFO/BFD/BMI_FAKE_DEVICE + fake accessor opt-in. */
int bfo_fake_registry_compiled(void);
#ifdef __cplusplus
}
#endif
#endif
