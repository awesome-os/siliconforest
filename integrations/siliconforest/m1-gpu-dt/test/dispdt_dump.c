/* SPDX-License-Identifier: MIT */
/*
 * Prints the display tree dispdt.c builds from the saved device tree in the format of the
 * Linux dump (`== path`, then `name hex` per property, values in FDT byte order), for
 * test/compare_disp.py. Arguments: fw major minor patch, fb base, width, height, stride,
 * depth, then optionally the five mapped addresses (dcp_data region57 region94 region95
 * framebuffer) as boot dc12 measured them, to fill iommu-addresses.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dispdt.h"
#include "fixture.h"

static const void *getprop(void *ctx, const char *path, const char *name, uint32_t *len)
{
    (void)ctx;
    for (unsigned i = 0; i < fixture_nprops; i++) {
        if (!strcmp(fixture_props[i].path, path) && !strcmp(fixture_props[i].name, name)) {
            *len = fixture_props[i].len;
            return fixture_props[i].value;
        }
    }
    return NULL;
}

static struct dispdt_tree t;

int main(int argc, char **argv)
{
    struct gpudt_adt_ops adt = {NULL, getprop};
    struct dispdt_config cfg = {.chip_id = GPUDT_CHIP_T8103, .phandle_base = DISPDT_DARWIN_PHANDLE_BASE};

    if (argc < 9)
        return 2;
    for (int i = 0; i < 3; i++)
        cfg.fw_version[i] = (uint32_t)strtoul(argv[1 + i], NULL, 0);
    cfg.fw_version_len = 3;
    cfg.fb_base = strtoull(argv[4], NULL, 0);
    cfg.fb_width = (uint32_t)strtoul(argv[5], NULL, 0);
    cfg.fb_height = (uint32_t)strtoul(argv[6], NULL, 0);
    cfg.fb_stride = (uint32_t)strtoul(argv[7], NULL, 0);
    cfg.fb_depth = (uint32_t)strtoul(argv[8], NULL, 0);
    for (int i = 0; i < 5 && 9 + i < argc; i++) {
        uint64_t iova = strtoull(argv[9 + i], NULL, 0);

        /* one address per region: boot dc12 found the regions at the same address in every DART that maps them */
        cfg.dcp_iova[i] = cfg.disp_iova[i] = cfg.piodma_iova[i] = (struct dispdt_iova){true, iova};
    }
    int ret = dispdt_build_t8103(&adt, &cfg, &t);
    if (ret != DISPDT_OK) {
        fprintf(stderr, "dispdt_build_t8103: %d\n", ret);
        return 1;
    }
    for (int id = 0; id < DISPDT_NODE_COUNT; id++) {
        const struct dispdt_node *n = &t.nodes[id];

        if (!n->present || n->existing)
            continue;
        printf("== %s%s%s\n", n->parent >= 0 ? t.nodes[n->parent].name : "", n->parent >= 0 ? "/" : "", n->name);
        for (uint32_t i = 0; i < t.nprops; i++) {
            const struct dispdt_prop *p = &t.props[i];

            if (p->node != id)
                continue;
            printf("%s ", p->name);
            for (unsigned k = 0; k < p->len; k++)
                printf("%02x", p->value[k]);
            printf("\n");
        }
    }
    return 0;
}
