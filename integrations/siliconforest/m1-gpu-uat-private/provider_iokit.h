/* Original code; license selection pending. No runtime entry point. */
#ifndef DUAT_IOKIT_H
#define DUAT_IOKIT_H
#include "private.h"
#ifdef __cplusplus
extern "C" {
#endif
/* No provider context needed: descriptor itself is the owner reference.
 * There are no globals, callbacks, kext/service lifecycle or user mappings. */
struct duat_ops duat_iokit_ops(void);
#ifdef __cplusplus
}
#endif
#endif
