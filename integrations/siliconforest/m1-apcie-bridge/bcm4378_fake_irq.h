/* Original host-only logical interrupt source, not a hardware MSI driver. */
#ifndef DL_BCM4378_FAKE_IRQ_H
#define DL_BCM4378_FAKE_IRQ_H
#include "bringup.h"
#ifdef __cplusplus
extern "C" {
#endif

enum bmi_result {
    BMI_FAKE_OK = 0, BMI_GATED = 100, BMI_PHY_NOT_READY = 101,
    BMI_LINK_DOWN = 102, BMI_EMPTY = 103, BMI_BUSY = 104,
    BMI_ARGUMENT = -100, BMI_OWNER = -101, BMI_STATE = -102,
    BMI_IO = -103, BMI_LIMIT = -104
};
struct bmi_profile {
    uint64_t address; /* numerical message address, never a dereferenced pointer */
    uint32_t rid, vector_base, vector_count, aic_base;
};
/* Caller-owned ordinary host-memory fake source file. No hardware register
 * offsets/formats are claimed for these fields. Changes count one logical
 * commit per admitted operation, NOT a device write or AIC interrupt. */
struct bmi_source {
    struct apb_io io;
    struct bmi_profile profile;
    uint32_t bound, armed, closed, allocated, masked, pending;
    uint32_t received, delivered, commits;
};

/* Exact J293 fake profile: RID0x100, message address0xfffff000, port0 vectors
 * 0..7, AIC base704. Observed ADT metadata, not a hardware routing proof.
 * Source must start zeroed and cannot reopen. All operations serialized;
 * source/profile/accessor/output must be stable and nonaliasing. */
int bmi_open(struct bmi_source *, const struct apb_io *checked_fake, const struct bmi_profile *);
/* Initially all allocated sources masked; no fabricated event is enqueued. */
int bmi_arm(struct bmi_source *, uint32_t allocated_bitmap);
int bmi_set_mask(struct bmi_source *, uint32_t vector, int masked);
/* Explicit fake message injection only. Masked events latch/coalesce pending;
 * unknown address/data/unallocated vector refuses without changing state. */
int bmi_receive(struct bmi_source *, uint64_t address, uint32_t data);
/* One eligible pending bit only, lowest first. Acknowledges only that fake
 * bit; returns its vector/AIC number as arithmetic output. No handler or
 * actual interrupt delivery. Outputs unchanged on every refusal/EMPTY. */
int bmi_dispatch_one(struct bmi_source *, uint32_t *vector, uint32_t *aic_irq);
/* Requires every allocated source masked and pending empty. Does not clear
 * events, claim actual interrupt drain, or free caller storage. */
int bmi_close(struct bmi_source *);

/* Every fake operation checks PHY reset/clock bits, RUN/LINK and PCI identity
 * before a logical commit. Default/KERNEL operations only return GATED.
 * BMI_FAKE_DEVICE with KERNEL, or shared-PHY/PMGR write escape, is refused. */
int bmi_fake_source_compiled(void);
#ifdef __cplusplus
}
#endif
#endif
