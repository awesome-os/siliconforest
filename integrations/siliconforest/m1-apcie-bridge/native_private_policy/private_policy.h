/* Original configuration-only native join; no backing/effect/runtime admission. */
#ifndef BCM_NATIVE_PRIVATE_POLICY_H
#define BCM_NATIVE_PRIVATE_POLICY_H
#include "../native_policy_reader/policy_reader.h"
#include "../owned_control/owned_control.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NPP_VM_PRIVATE_PROFILE 1u
enum npp_status { NPP_OK=0, NPP_ARGUMENT=-300, NPP_BUSY=-301,
    NPP_USED=-302, NPP_READ=-303, NPP_POLICY=-304,
    NPP_CONFIGURE=-305, NPP_CHANGED=-306 };
struct npp_expected {
    struct nps_expected identity;
    uint64_t platform_domain, dart_domain, irq_domain; /* trusted labels only */
    uint32_t profile, reserved;
};
/* No receipt, token, descriptor, CPU/PA view or hardware permission escapes. */
struct npp_result {
    uint32_t version, profile, ram_count, reserved_count, control_pages, queries;
    struct npp_expected expected;
};
struct npp_workspace {
    struct npp_expected captured;
    struct npr_workspace reader;
    struct nps_result facts;
    struct bpl_policy policy, policy_before;
    struct boc_workspace control;
    struct boc_receipt receipt;
    struct bpl_range rounded[NPS_UNION_MAX];
    struct npp_result staged;
    int reader_status, configure_status;
};
/* One resident native attempt, no reset/retry. Invalid pure arguments refuse
 * before consuming it. All other failures preserve out but may consume pin or
 * leave a configured/retained native owner; never infer rollback from error.
 * Expected/workspace/out must be accessible and disjoint; workspace OFF STACK.
 * Trusted boot serialization/quiescence, wired code/context/storage and one
 * domain owner are caller obligations, not established by this call. Profile1
 * permits only kernel-VM-owned PRIVATE IOBuffer backing inside the inherited
 * envelope with known source guards. It is NOT global exclusion completeness,
 * DMA authority, or permission to invoke an external effect. No allocation,
 * acquire, effect, kext entrypoint, initializer or destructor is introduced. */
int npp_native_configure_once(const struct npp_expected *,
                              struct npp_workspace *,struct npp_result *);
#ifdef __cplusplus
}
#endif
#endif
