/* Version-1 diagnostic wire layout for PDApcieBridgeObservationV1 (84 bytes). */
#ifndef DL_APCIE_OBSERVE_LAYOUT_H
#define DL_APCIE_OBSERVE_LAYOUT_H
#include "observe.h"
#ifdef __cplusplus
#define APO_LAYOUT_ASSERT(test) static_assert((test), #test)
#define APO_LAYOUT_ALIGN(type) alignof(type)
#else
#define APO_LAYOUT_ASSERT(test) _Static_assert((test), #test)
#define APO_LAYOUT_ALIGN(type) _Alignof(type)
#endif
APO_LAYOUT_ASSERT(sizeof(struct apo_snapshot) == 84);
APO_LAYOUT_ASSERT(APO_LAYOUT_ALIGN(struct apo_snapshot) == 4);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, version) == 0);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, reads_attempted) == 4);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, stage_reached) == 8);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, valid_mask) == 12);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, result) == 16);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, last_region) == 20);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, last_offset) == 24);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, power) == 28);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, power_final) == 44);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, phy_ctrl) == 60);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, rc_phyif_ctrl) == 64);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, port0_appclk) == 68);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, port0_reset) == 72);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, port0_status) == 76);
APO_LAYOUT_ASSERT(offsetof(struct apo_snapshot, port0_linksts) == 80);
#undef APO_LAYOUT_ALIGN
#undef APO_LAYOUT_ASSERT
#endif
