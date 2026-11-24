/* Host-test stand-in for <mach/kern_return.h>: values match XNU. */
#ifndef SFGOP_HOSTTEST_KERN_RETURN_H
#define SFGOP_HOSTTEST_KERN_RETURN_H
#include <sys/cdefs.h>
typedef int kern_return_t;
#define KERN_SUCCESS          0
#define KERN_INVALID_ARGUMENT 4
#define KERN_FAILURE          5
#define KERN_NOT_SUPPORTED    46
#endif
