/* SPDX-License-Identifier: MIT */
/*
 * Host test of kernel/sf_linux_fwnode.c: the calls the T8103 driver path makes through the Rust
 * `of` and `property` abstractions, served from the library's tree. The of_* functions below
 * stand in for sf_linux_of.c and return FDT-order bytes, as that file does after its conversion.
 */
#include <stdio.h>

#include <linux/property.h>

#include "fixture.h"
#include "gpudt.h"

/* sf_linux_fwnode.c */
struct fwnode_handle *__dev_fwnode(struct device *dev);
int fwnode_property_read_u32_array(const struct fwnode_handle *, const char *, u32 *, size_t);
int fwnode_property_read_u64_array(const struct fwnode_handle *, const char *, u64 *, size_t);
int fwnode_property_match_string(const struct fwnode_handle *, const char *, const char *);
bool fwnode_property_present(const struct fwnode_handle *, const char *);
const char *fwnode_get_name(const struct fwnode_handle *);
unsigned int fwnode_count_parents(const struct fwnode_handle *);
struct fwnode_handle *fwnode_get_next_child_node(const struct fwnode_handle *, struct fwnode_handle *);
int of_reserved_mem_region_to_resource_byname(const struct device_node *, const char *, struct resource *);
bool of_node_is_root(const struct device_node *np);

static struct gpudt_tree tree;
static struct device_node nodes[GPUDT_NODE_COUNT + 1]; /* last: the root */
#define ROOT (&nodes[GPUDT_NODE_COUNT])

static int id_of(const struct device_node *np)
{
    return (int)(np - nodes);
}

const void *of_get_property(const struct device_node *np, const char *name, int *lenp)
{
    const struct gpudt_prop *p = np && np != ROOT ? gpudt_find(&tree, id_of(np), name) : NULL;

    if (p && lenp)
        *lenp = p->len;
    return p ? p->value : NULL;
}

struct property *of_find_property(const struct device_node *np, const char *name, int *lenp)
{
    return (struct property *)of_get_property(np, name, lenp);
}

int of_property_read_string(const struct device_node *np, const char *name, const char **out)
{
    const char *v = of_get_property(np, name, NULL);

    if (!v)
        return -EINVAL;
    *out = v;
    return 0;
}

int of_property_match_string(const struct device_node *np, const char *name, const char *str)
{
    int len, i = 0;
    const char *v = of_get_property(np, name, &len);

    for (const char *p = v; v && p < v + len; p += strlen(p) + 1, i++)
        if (!strcmp(p, str))
            return i;
    return -ENODATA;
}

struct device_node *of_get_next_child(const struct device_node *np, struct device_node *prev)
{
    for (int i = prev ? id_of(prev) + 1 : 0; i < GPUDT_NODE_COUNT; i++)
        if (nodes[i].parent == np)
            return &nodes[i];
    return NULL;
}

struct device_node *of_get_child_by_name(const struct device_node *np, const char *name)
{
    for (struct device_node *c = of_get_next_child(np, NULL); c; c = of_get_next_child(np, c))
        if (!strcmp(c->name, name))
            return c;
    return NULL;
}

int of_parse_phandle_with_args(const struct device_node *np, const char *list, const char *cells,
                               int index, struct of_phandle_args *out)
{
    int len;
    const uint8_t *v = of_get_property(np, list, &len);

    (void)cells;
    if (!v || index * 4 + 4 > len)
        return -ENOENT;
    uint32_t ph = (uint32_t)v[index * 4] << 24 | v[index * 4 + 1] << 16 | v[index * 4 + 2] << 8 | v[index * 4 + 3];
    for (int i = 0; i < GPUDT_NODE_COUNT; i++)
        if (nodes[i].phandle == ph && ph) {
            out->np = &nodes[i];
            out->args_count = 0;
            return 0;
        }
    return -EINVAL;
}

struct device_node *of_parse_phandle(const struct device_node *np, const char *list, int index)
{
    struct of_phandle_args a;
    return of_parse_phandle_with_args(np, list, NULL, index, &a) ? NULL : a.np;
}

bool of_device_is_available(const struct device_node *np)
{
    const char *s = of_get_property(np, "status", NULL);
    return !s || !strcmp(s, "okay") || !strcmp(s, "ok");
}

int of_address_to_resource(struct device_node *np, int index, struct resource *res)
{
    int len;
    const uint8_t *v = of_get_property(np, "reg", &len);
    u64 a = 0, s = 0;

    if (!v || (index + 1) * 16 > len)
        return -EINVAL;
    for (int i = 0; i < 8; i++) {
        a = a << 8 | v[index * 16 + i];
        s = s << 8 | v[index * 16 + 8 + i];
    }
    res->start = a;
    res->end = a + s - 1;
    return 0;
}

void *sfos_dt_root(void)
{
    return ROOT;
}

struct device_node *sf_of_node_for_entry(void *entry)
{
    return entry;
}

static const void *getprop(void *ctx, const char *path, const char *name, uint32_t *len)
{
    (void)ctx;
    for (unsigned i = 0; i < fixture_nprops; i++)
        if (!strcmp(fixture_props[i].path, path) && !strcmp(fixture_props[i].name, name)) {
            *len = fixture_props[i].len;
            return fixture_props[i].value;
        }
    return NULL;
}

static int failures;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL: %s\n", msg);                                                             \
        }                                                                                          \
    } while (0)

int main(void)
{
    struct gpudt_adt_ops ops = {NULL, getprop};
    struct gpudt_config cfg = {.chip_id = GPUDT_CHIP_T8103, .have_core_mask = true, .core_mask = 0xff,
                               .phandle_base = GPUDT_DARWIN_PHANDLE_BASE};
    struct device dev = {&nodes[GPUDT_NODE_GPU]};
    struct fwnode_handle *fw, *opp = NULL, *table;
    struct resource res;
    u32 v[4];
    u64 hz;
    int n = 0;

    if (gpudt_build_t8103(&ops, &cfg, &tree) != GPUDT_OK)
        return 1;
    ROOT->name = ROOT->full_name = "";
    for (int i = 0; i < GPUDT_NODE_COUNT; i++) {
        nodes[i].name = nodes[i].full_name = tree.nodes[i].name;
        nodes[i].parent = tree.nodes[i].parent < 0 ? ROOT : &nodes[tree.nodes[i].parent];
        nodes[i].phandle = tree.nodes[i].phandle;
    }
    fw = __dev_fwnode(&dev);

    /* driver.rs: property_read_array_vec("apple,firmware-compat", 3): a count call, then the read */
    CHECK(fwnode_property_read_u32_array(fw, "apple,firmware-compat", NULL, 0) == 3, "compat count");
    CHECK(fwnode_property_read_u32_array(fw, "apple,firmware-compat", v, 3) == 0 && v[0] == 13 && v[1] == 5 &&
          v[2] == 0, "compat values");
    CHECK(fwnode_property_read_u32_array(fw, "apple,firmware-compat", v, 4) == -EOVERFLOW, "compat too long");
    /* gpu.rs: property_read::<u32>("debug,hw-cal-a-size").or(0): absent */
    CHECK(fwnode_property_read_u32_array(fw, "debug,hw-cal-a-size", v, 1) == -EINVAL, "absent property");
    CHECK(fwnode_property_present(fw, "apple,power-zones") && !fwnode_property_present(fw, "apple,se-ki"),
          "presence");
    CHECK(fwnode_property_match_string(fw, "memory-region-names", "handoff") == 2, "match string");
    CHECK(!strcmp(fwnode_get_name(fw), "gpu@206400000") && fwnode_count_parents(fw) == 1, "name and depth");
    CHECK(of_node_is_root(ROOT) && !of_node_is_root(dev.of_node), "root");

    /* mmu.rs / gpu.rs: regions by name */
    CHECK(of_reserved_mem_region_to_resource_byname(dev.of_node, "ttbs", &res) == 0 &&
          res.end - res.start + 1 == 0x4000, "ttbs region");
    CHECK(of_reserved_mem_region_to_resource_byname(dev.of_node, "pagetables", &res) == 0 &&
          res.end - res.start + 1 == 0x40000, "pagetables region");
    CHECK(of_reserved_mem_region_to_resource_byname(dev.of_node, "handoff", &res) == 0, "handoff region");
    CHECK(of_reserved_mem_region_to_resource_byname(dev.of_node, "hw-cal-a", &res) == -ENODEV, "disabled region");
    CHECK(of_reserved_mem_region_to_resource_byname(dev.of_node, "l2-pagetables", &res) < 0, "unknown region");

    /* hw/mod.rs load_opp: children of the table in order, opp-hz as u64 */
    table = &of_parse_phandle(dev.of_node, "operating-points-v2", 0)->fwnode;
    while ((opp = fwnode_get_next_child_node(table, opp)) != NULL) {
        CHECK(fwnode_property_read_u64_array(opp, "opp-hz", &hz, 1) == 0, "opp-hz");
        CHECK(fwnode_property_read_u32_array(opp, "opp-microvolt", NULL, 0) == 1, "one voltage per point");
        if (n == 6)
            CHECK(hz == 1278000000ull, "top frequency");
        n++;
    }
    CHECK(n == GPUDT_T8103_NUM_OPPS, "seven operating points");
    printf("fwnode test (kernel/sf_linux_fwnode.c against the library's tree): %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
