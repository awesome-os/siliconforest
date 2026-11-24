/* Exact declarations from pinned e7 XNU arm/pmap/pmap.h:516 and vm/pmap.h:755.
 * Required imports are Unsupported.exports _kernel_pmap/_pmap_find_phys.
 * Build MUST preinclude exact private mach/vm_types.h then arm/locks.h before
 * public headers. No invented pmap type or opaque mutex layout is supplied. */
#ifndef BCM_CONTROL_NATIVE_PMAP_BRIDGE_H
#define BCM_CONTROL_NATIVE_PMAP_BRIDGE_H
#include <mach/vm_types.h>
extern "C" {
extern const pmap_t kernel_pmap;
extern ppnum_t pmap_find_phys(pmap_t,addr64_t);
}
#endif
