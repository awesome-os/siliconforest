// SPDX-License-Identifier: MIT
/*
 * sf_of_iter.c - of_find_compatible_node() with Linux's `from` semantics, for the layer.
 *
 * The layer's of_find_compatible_node() ignores `from` and always returns the first match
 * (sf_linux_of.c: "the first node in tree order ... the search starts at the root"), so
 * for_each_compatible_node() and for_each_matching_node() never end once one node matches.
 * This walks the device tree in pre-order with the layer's own child iteration and returns
 * the first match after `from` (or from the root when `from` is NULL), NULL at the end.
 *
 * Proposed for the layer as the body of of_find_compatible_node(); until then a kext that
 * needs the iteration compiles this file and uses sf_of_find_compatible_node_from().
 */
#include <linux/of.h>

/* the layer's own: the device_node mirroring an IORegistryEntry, and the tree's root */
struct device_node *sf_of_node_for_entry(void *entry);
void *sfos_dt_root(void);

static struct device_node *sf_of_next(struct device_node *root, struct device_node *np)
{
	struct device_node *next = of_get_next_child(np, NULL);

	if (next)
		return next;
	while (np && np != root && np->parent) {
		next = of_get_next_child(np->parent, np);
		if (next)
			return next;
		np = np->parent;
	}
	return NULL;
}

struct device_node *sf_of_find_compatible_node_from(struct device_node *from, const char *type,
						    const char *compat)
{
	struct device_node *root = sf_of_node_for_entry(sfos_dt_root());
	struct device_node *np;

	if (!root || !compat)
		return NULL;
	for (np = from ? sf_of_next(root, from) : root; np; np = sf_of_next(root, np))
		if (of_device_is_compatible(np, compat))
			return np;
	return NULL;
}
