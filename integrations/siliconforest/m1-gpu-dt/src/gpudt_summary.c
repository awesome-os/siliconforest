/* SPDX-License-Identifier: MIT */
/*
 * m1-gpu-dt: the summary of a built tree, as the SFGpuDT kext logs it and the
 * host test predicts it. Kept apart from gpudt.c because it needs snprintf.
 */
#include "gpudt.h"

#include <string.h>

#ifdef KERNEL
#include <libkern/libkern.h>
#else
#include <stdio.h>
#endif

static uint64_t be_cells(const struct gpudt_prop *p, unsigned first, unsigned n)
{
    uint64_t v = 0;

    for (unsigned i = 4 * first; p && i < 4 * (first + n) && i < p->len; i++)
        v = v << 8 | p->value[i];
    return v;
}

void gpudt_summary(const struct gpudt_tree *t, void (*emit)(void *ctx, const char *line), void *ctx)
{
    static const struct {
        const char *label;
        int node;
    } groups[] = {{"gpu", GPUDT_NODE_GPU},
                  {"opp-table-gpu", GPUDT_NODE_OPP_TABLE},
                  {"reserved-memory", GPUDT_NODE_RESERVED_MEMORY},
                  {"chosen", GPUDT_NODE_CHOSEN}},
      regions[] = {{"handoff", GPUDT_NODE_UAT_HANDOFF},
                   {"pagetables", GPUDT_NODE_UAT_PAGETABLES},
                   {"ttbs", GPUDT_NODE_UAT_TTBS}};
    char line[160];
    uint32_t n_all, n_linux, h_all, h_linux;

    h_all = gpudt_checksum(t, GPUDT_CHECKSUM_ALL, -1, &n_all);
    h_linux = gpudt_checksum(t, GPUDT_CHECKSUM_LINUX, -1, &n_linux);
    snprintf(line, sizeof(line), "summary v1: %u properties fnv1a 0x%08x; linux-comparable %u properties fnv1a 0x%08x",
             n_all, h_all, n_linux, h_linux);
    emit(ctx, line);
    for (unsigned i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
        h_all = gpudt_checksum(t, GPUDT_CHECKSUM_ALL, groups[i].node, &n_all);
        h_linux = gpudt_checksum(t, GPUDT_CHECKSUM_LINUX, groups[i].node, &n_linux);
        snprintf(line, sizeof(line), "subtree %s: %u properties 0x%08x; linux-comparable %u properties 0x%08x",
                 groups[i].label, n_all, h_all, n_linux, h_linux);
        emit(ctx, line);
    }
    snprintf(line, sizeof(line), "firmware %s version %u.%u.%u compat %u.%u.%u", t->fw_string,
             t->fw_version[0], t->fw_version[1], t->fw_version[2], t->fw_compat[0], t->fw_compat[1],
             t->fw_compat[2]);
    emit(ctx, line);
    snprintf(line, sizeof(line), "cores %u mask 0x%02x (%s) core-leak 0x%08x sram-leak 0x%08x", t->core_count,
             t->core_mask, t->core_mask_assumed ? "ASSUMED, not read" : "read",
             (uint32_t)be_cells(gpudt_find(t, GPUDT_NODE_GPU, "apple,core-leak-coef"), 0, 1),
             (uint32_t)be_cells(gpudt_find(t, GPUDT_NODE_GPU, "apple,sram-leak-coef"), 0, 1));
    emit(ctx, line);
    for (int i = 0; i < GPUDT_T8103_NUM_OPPS; i++) {
        enum gpudt_node_id node = (enum gpudt_node_id)(GPUDT_NODE_OPP0 + i);

        snprintf(line, sizeof(line), "opp%d %llu Hz %u uV %u uW", i,
                 (unsigned long long)be_cells(gpudt_find(t, node, "opp-hz"), 0, 2),
                 (uint32_t)be_cells(gpudt_find(t, node, "opp-microvolt"), 0, 1),
                 (uint32_t)be_cells(gpudt_find(t, node, "opp-microwatt"), 0, 1));
        emit(ctx, line);
    }
    for (unsigned i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
        const struct gpudt_prop *reg = gpudt_find(t, (enum gpudt_node_id)regions[i].node, "reg");

        snprintf(line, sizeof(line), "region %s 0x%llx size 0x%llx", regions[i].label,
                 (unsigned long long)be_cells(reg, 0, 2), (unsigned long long)be_cells(reg, 2, 2));
        emit(ctx, line);
    }
}
