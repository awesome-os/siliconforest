#ifndef DARWIN_LAB_MEMORY_PROBE_IOCTL_H
#define DARWIN_LAB_MEMORY_PROBE_IOCTL_H
#include "memory_probe_protocol.h"
#if defined(__APPLE__)
#include <sys/ioccom.h>
#else
#include <sys/ioctl.h>
#endif
#define DLMP_INFO _IOWR('D', 0x70, struct dlmp_info)
#define DLMP_ALLOC _IOWR('D', 0x71, struct dlmp_alloc)
#define DLMP_STATS _IOWR('D', 0x72, struct dlmp_stats)
#define DLMP_COLLECT _IOWR('D', 0x73, struct dlmp_collect)
#endif
