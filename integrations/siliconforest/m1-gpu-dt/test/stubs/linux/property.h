/* SPDX-License-Identifier: MIT */
/* Host stand-in for linux/property.h. Test only. */
#ifndef STUB_LINUX_PROPERTY_H
#define STUB_LINUX_PROPERTY_H
#include <linux/of.h>
#define NR_FWNODE_REFERENCE_ARGS 16
struct fwnode_reference_args {
    struct fwnode_handle *fwnode;
    unsigned int nargs;
    u64 args[NR_FWNODE_REFERENCE_ARGS];
};
#endif
