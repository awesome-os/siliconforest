/* Darwin has no <sys/sysmacros.h>.  XNU's dev_t is 8-bit major, 24-bit minor, and <sys/types.h>
 * already defines major()/minor()/makedev() that way; this header only makes the Linux include
 * line resolve.  Same shim as SiliconForest's nix/pkgs/x11/libdrm.nix. */
#ifndef SILICONFOREST_SYSMACROS_H
#define SILICONFOREST_SYSMACROS_H
#include <sys/types.h>
#ifndef major
#define major(dev) ((int)(((dev) >> 24) & 0xff))
#endif
#ifndef minor
#define minor(dev) ((int)((dev) & 0xffffff))
#endif
#ifndef makedev
#define makedev(major, minor) ((dev_t)((((dev_t)(major)) << 24) | ((dev_t)(minor) & 0xffffff)))
#endif
#endif
