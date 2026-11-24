#ifndef DARWIN_LAB_PROVIDER_IOKIT_H
#define DARWIN_LAB_PROVIDER_IOKIT_H
#include "memory_probe.h"
/* Candidate kernel provider. Not built against XNU in the development container.
 * Mapped allocations intentionally remain quarantined until reboot. */
dl_probe_ops dl_iokit_probe_ops(void);
#endif
