// SPDX-License-Identifier: MIT
// Proposed addition to SiliconForest fbsdkpi/sf/lkpi (next to sf_linux_of.c): the firmware-node
// property readers, of_root and friends, and the reserved-memory lookup that the Rust `kernel::of`
// and `kernel::device::property` abstractions bind and sf_linux_of.c does not have.
//
// NOT part of any build of that layer yet. It is compiled and exercised on the host by
// `make test` against test/stubs/, which stands in for the layer's headers; it has not been
// compiled against the real ones. It needs one header change, in lkpi-override/linux/of.h:
//
//     struct device_node { ...; struct fwnode_handle fwnode; };
//
// Only device-tree nodes are firmware nodes here: every fwnode_handle is the member of a
// device_node, so no ops table is needed.

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/property.h>

struct device_node *of_root;
struct device_node *of_chosen;
struct device_node *of_aliases;
struct device_node *of_stdout;

struct device_node *sf_of_node_for_entry(void *entry);
void *sfos_dt_root(void);

// to be called once from the layer's init, before any driver probes
void
sf_of_globals_init(void)
{
	of_root = sf_of_node_for_entry(sfos_dt_root());
	of_chosen = of_get_child_by_name(of_root, "chosen");
	of_aliases = of_get_child_by_name(of_root, "aliases");
	// of_stdout stays NULL: Apple's device tree has no stdout-path
}

bool
of_node_is_root(const struct device_node *np)
{
	return (np != NULL && np->parent == NULL);
}

static struct device_node *
fw_np(const struct fwnode_handle *fwnode)
{
	if (fwnode == NULL)
		return (NULL);
	return ((struct device_node *)((char *)fwnode - offsetof(struct device_node, fwnode)));
}

static struct fwnode_handle *
np_fw(struct device_node *np)
{
	return (np != NULL ? &np->fwnode : NULL);
}

struct fwnode_handle *
__dev_fwnode(struct device *dev)
{
	return (dev != NULL && dev->of_node != NULL ? &dev->of_node->fwnode : NULL);
}

// nodes live as long as the kext (sf_of_node_for_entry), so references are not counted
struct fwnode_handle *
fwnode_handle_get(struct fwnode_handle *fwnode)
{
	return (fwnode);
}

void
fwnode_handle_put(struct fwnode_handle *fwnode)
{
}

bool
fwnode_property_present(const struct fwnode_handle *fwnode, const char *name)
{
	return (of_find_property(fw_np(fwnode), name, NULL) != NULL);
}

bool
fwnode_property_read_bool(const struct fwnode_handle *fwnode, const char *name)
{
	return (fwnode_property_present(fwnode, name));
}

// Linux semantics: with val == NULL the number of elements; -EINVAL without the property,
// -ENODATA without a value, -EOVERFLOW when it is shorter than asked or not whole elements
static int
read_ints(const struct fwnode_handle *fwnode, const char *name, void *val, size_t nval, int size)
{
	int len;
	const uint8_t *v = of_get_property(fw_np(fwnode), name, &len);

	if (v == NULL)
		return (-EINVAL);
	if (len == 0)
		return (-ENODATA);
	if (val == NULL)
		return (len % size == 0 ? len / size : -EOVERFLOW);
	if (nval * size > (size_t)len)
		return (-EOVERFLOW);
	for (size_t i = 0; i < nval; i++) {
		uint64_t x = 0;

		for (int b = 0; b < size; b++)
			x = x << 8 | v[i * size + b];
		switch (size) {
		case 1: ((uint8_t *)val)[i] = x; break;
		case 2: ((uint16_t *)val)[i] = x; break;
		case 4: ((uint32_t *)val)[i] = x; break;
		default: ((uint64_t *)val)[i] = x; break;
		}
	}
	return (0);
}

int
fwnode_property_read_u8_array(const struct fwnode_handle *fwnode, const char *name, u8 *val, size_t nval)
{
	return (read_ints(fwnode, name, val, nval, 1));
}

int
fwnode_property_read_u16_array(const struct fwnode_handle *fwnode, const char *name, u16 *val, size_t nval)
{
	return (read_ints(fwnode, name, val, nval, 2));
}

int
fwnode_property_read_u32_array(const struct fwnode_handle *fwnode, const char *name, u32 *val, size_t nval)
{
	return (read_ints(fwnode, name, val, nval, 4));
}

int
fwnode_property_read_u64_array(const struct fwnode_handle *fwnode, const char *name, u64 *val, size_t nval)
{
	return (read_ints(fwnode, name, val, nval, 8));
}

int
fwnode_property_read_string(const struct fwnode_handle *fwnode, const char *name, const char **val)
{
	int ret = of_property_read_string(fw_np(fwnode), name, val);

	return (ret == -ENODATA ? -EINVAL : ret);
}

int
fwnode_property_match_string(const struct fwnode_handle *fwnode, const char *name, const char *string)
{
	if (of_find_property(fw_np(fwnode), name, NULL) == NULL)
		return (-EINVAL);
	return (of_property_match_string(fw_np(fwnode), name, string));
}

int
fwnode_property_get_reference_args(const struct fwnode_handle *fwnode, const char *prop,
    const char *nargs_prop, unsigned int nargs, unsigned int index, struct fwnode_reference_args *args)
{
	struct of_phandle_args oa;
	int ret;

	// without a cells property Linux takes a fixed count, which of_parse_phandle_with_args here does not do
	if (nargs_prop == NULL && nargs != 0)
		return (-EOPNOTSUPP);
	ret = of_parse_phandle_with_args(fw_np(fwnode), prop, nargs_prop, index, &oa);
	if (ret != 0)
		return (ret);
	if (args != NULL) {
		args->fwnode = np_fw(oa.np);
		args->nargs = oa.args_count;
		for (int i = 0; i < oa.args_count && i < NR_FWNODE_REFERENCE_ARGS; i++)
			args->args[i] = oa.args[i];
	}
	return (0);
}

const char *
fwnode_get_name(const struct fwnode_handle *fwnode)
{
	struct device_node *np = fw_np(fwnode);

	return (np != NULL ? np->full_name : NULL);
}

const char *
fwnode_get_name_prefix(const struct fwnode_handle *fwnode)
{
	struct device_node *np = fw_np(fwnode);

	// as Linux prints paths: nothing before the root, a slash before every other node
	return (np == NULL || np->parent == NULL ? "" : "/");
}

struct fwnode_handle *
fwnode_get_named_child_node(const struct fwnode_handle *fwnode, const char *childname)
{
	return (np_fw(of_get_child_by_name(fw_np(fwnode), childname)));
}

struct fwnode_handle *
fwnode_get_next_child_node(const struct fwnode_handle *fwnode, struct fwnode_handle *child)
{
	return (np_fw(of_get_next_child(fw_np(fwnode), fw_np(child))));
}

unsigned int
fwnode_count_parents(const struct fwnode_handle *fwnode)
{
	unsigned int n = 0;

	for (struct device_node *np = fw_np(fwnode); np != NULL && np->parent != NULL; np = np->parent)
		n++;
	return (n);
}

struct fwnode_handle *
fwnode_get_nth_parent(struct fwnode_handle *fwnode, unsigned int depth)
{
	struct device_node *np = fw_np(fwnode);

	while (np != NULL && depth-- > 0)
		np = np->parent;
	return (np_fw(np));
}

// Linux looks in its reserved_mem table first and falls back to the node's "reg"; only the
// fallback exists here
int
of_reserved_mem_region_to_resource(const struct device_node *np, unsigned int idx, struct resource *res)
{
	struct device_node *target;

	if (np == NULL)
		return (-EINVAL);
	target = of_parse_phandle(np, "memory-region", idx);
	if (target == NULL || !of_device_is_available(target))
		return (-ENODEV);
	return (of_address_to_resource(target, 0, res));
}

int
of_reserved_mem_region_to_resource_byname(const struct device_node *np, const char *name,
    struct resource *res)
{
	int idx;

	if (name == NULL)
		return (-EINVAL);
	idx = of_property_match_string(np, "memory-region-names", name);
	if (idx < 0)
		return (idx);
	return (of_reserved_mem_region_to_resource(np, idx, res));
}
