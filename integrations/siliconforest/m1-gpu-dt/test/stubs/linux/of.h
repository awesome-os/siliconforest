/* SPDX-License-Identifier: MIT */
/* Host stand-in: the parts of lkpi-override/linux/of.h sf_linux_fwnode.c uses, plus the proposed fwnode member. */
#ifndef STUB_LINUX_OF_H
#define STUB_LINUX_OF_H
#include <linux/kernel.h>

struct fwnode_handle {
    struct fwnode_handle *secondary;
};

struct property;
struct device_node {
    const char *name;
    const char *full_name;
    u32 phandle;
    struct property *properties;
    struct device_node *parent;
    void *sf_entry;
    struct device_node *sf_next;
    struct fwnode_handle fwnode;
};

struct device {
    struct device_node *of_node;
};

struct resource {
    u64 start, end;
};

#define MAX_PHANDLE_ARGS 16
struct of_phandle_args {
    struct device_node *np;
    int args_count;
    uint32_t args[MAX_PHANDLE_ARGS];
};

struct property *of_find_property(const struct device_node *np, const char *name, int *lenp);
const void *of_get_property(const struct device_node *np, const char *name, int *lenp);
int of_property_read_string(const struct device_node *np, const char *name, const char **out);
int of_property_match_string(const struct device_node *np, const char *name, const char *str);
struct device_node *of_get_child_by_name(const struct device_node *np, const char *name);
struct device_node *of_get_next_child(const struct device_node *np, struct device_node *prev);
struct device_node *of_parse_phandle(const struct device_node *np, const char *list, int index);
int of_parse_phandle_with_args(const struct device_node *np, const char *list, const char *cells,
                               int index, struct of_phandle_args *out);
bool of_device_is_available(const struct device_node *np);
int of_address_to_resource(struct device_node *np, int index, struct resource *res);
#endif
