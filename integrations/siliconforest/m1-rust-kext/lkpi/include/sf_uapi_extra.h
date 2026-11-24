
/*
 * SiliconForest additions to the reduced uapi_helper.h (appended by the build).
 *
 * Linux's asm-generic/ioctl.h field layout, which rust/kernel/ioctl.rs builds ioctl numbers
 * from. There is no such header here: ioctl numbers on this system are BSD ones
 * (sys/ioccom.h, which drm-kmod's DRM_IOWR() and the userland use). The BSD encoding fits
 * Linux's scheme exactly with a 3-bit direction field at bit 29 and a 13-bit size, so the
 * Rust side produces the numbers the C side and userland expect:
 *     _IOC_NONE  = IOC_VOID >> 29      _IOC_READ = IOC_OUT >> 29     _IOC_WRITE = IOC_IN >> 29
 */
#include <sys/ioccom.h>

#define _IOC_NRBITS	8
#define _IOC_TYPEBITS	8
#define _IOC_SIZEBITS	13
#define _IOC_DIRBITS	3

#define _IOC_NRMASK	0xff
#define _IOC_TYPEMASK	0xff
#define _IOC_SIZEMASK	0x1fff
#define _IOC_DIRMASK	0x7

#define _IOC_NRSHIFT	0
#define _IOC_TYPESHIFT	8
#define _IOC_SIZESHIFT	16
#define _IOC_DIRSHIFT	29

#define _IOC_NONE	1U
#define _IOC_READ	2U
#define _IOC_WRITE	4U

_Static_assert(((unsigned long)_IOC_NONE << _IOC_DIRSHIFT) == IOC_VOID, "ioctl direction: none");
_Static_assert(((unsigned long)_IOC_READ << _IOC_DIRSHIFT) == IOC_OUT, "ioctl direction: read");
_Static_assert(((unsigned long)_IOC_WRITE << _IOC_DIRSHIFT) == IOC_IN, "ioctl direction: write");
_Static_assert(_IOC_SIZEMASK == IOCPARM_MASK, "ioctl size field");
_Static_assert(_IOWR('d', 0x42, int) ==
    (((unsigned long)(_IOC_READ | _IOC_WRITE) << _IOC_DIRSHIFT) | (sizeof(int) << _IOC_SIZESHIFT) |
     ('d' << _IOC_TYPESHIFT) | 0x42), "BSD _IOWR is not the Linux layout with these fields");
