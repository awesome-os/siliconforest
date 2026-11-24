/* SPDX-License-Identifier: MIT */
/*
 * Host harness for m1-gpu-dt: feeds the library from the generated fixture.
 *
 *   harness dump [--phandle-base N] [--mbox N] [--power N] [--core-mask M | --assume-all-cores]
 *       one line per property, read by compare.py
 *   harness kextlog [--core-mask M | --assume-all-cores]
 *       the lines the SFGpuDT kext logs for this device tree, with the kext's configuration
 *   harness selftest
 *       soft-float against the host FPU, m1n1's adjust_leakage() in real
 *       floats for every core count, and the refusal paths
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixture.h"
#include "gpudt.h"

struct filter {
    const char *hide_path, *hide_name; /* property to pretend is absent */
    const char *patch_name;            /* /arm-io/sgx property to replace */
    const unsigned char *patch_value;
    unsigned patch_len;
};

static const void *fixture_getprop(void *ctx, const char *path, const char *name, uint32_t *len)
{
    const struct filter *f = ctx;

    if (f && f->hide_name && !strcmp(path, f->hide_path) && !strcmp(name, f->hide_name))
        return NULL;
    if (f && f->patch_name && !strcmp(path, "/arm-io/sgx") && !strcmp(name, f->patch_name)) {
        *len = f->patch_len;
        return f->patch_value;
    }
    for (unsigned i = 0; i < fixture_nprops; i++) {
        if (!strcmp(fixture_props[i].path, path) && !strcmp(fixture_props[i].name, name)) {
            *len = fixture_props[i].len;
            return fixture_props[i].value;
        }
    }
    return NULL;
}

struct chosen_ctx {
    const unsigned char *s;
    unsigned len;
};

static const void *chosen_getprop(void *ctx, const char *path, const char *name, uint32_t *len)
{
    const struct chosen_ctx *c = ctx;

    if (!strcmp(path, "/chosen") && !strcmp(name, "firmware-version")) {
        *len = c->len;
        return c->s;
    }
    return fixture_getprop(NULL, path, name, len);
}

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            printf("FAIL: " __VA_ARGS__);                                                          \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint32_t bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static float flt(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* m1n1 adjust_leakage() as written, for one cluster; volatile keeps each step a separate float op */
static float m1n1_adjust_leakage(float val, uint32_t cores, uint32_t max, float uncore_fraction)
{
    volatile float uncore = val * uncore_fraction;
    volatile float core = val - uncore;
    volatile float ratio = cores / (float)max;
    volatile float prod = ratio * core;
    volatile float res = uncore + prod;
    return res;
}

/* the same with the last multiply-add fused, as a compiler contracting floating point would emit */
static float m1n1_adjust_leakage_fma(float val, uint32_t cores, uint32_t max, float uncore_fraction)
{
    volatile float uncore = val * uncore_fraction;
    volatile float core = val - uncore;
    volatile float ratio = cores / (float)max;
    return fmaf(ratio, core, uncore);
}

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;

static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}

/* a positive normal float with an exponent in [-20, 20] */
static uint32_t rnd_f32(void)
{
    return ((127 - 20 + rng() % 41) << 23) | (rng() & 0x7fffff);
}

static void selftest_softfloat(void)
{
    int before = failures;

    for (int i = 0; i < 2000000; i++) {
        uint32_t a = rnd_f32(), b = rnd_f32();
        volatile float fa = flt(a), fb = flt(b);
        volatile float m = fa * fb, d = fa / fb, s = fa + fb;

        CHECK(gpudt_f32_mul(a, b) == bits(m), "mul %08x %08x", a, b);
        CHECK(gpudt_f32_div(a, b) == bits(d), "div %08x %08x", a, b);
        CHECK(gpudt_f32_add(a, b) == bits(s), "add %08x %08x", a, b);
        if (fa >= fb) {
            volatile float x = fa - fb;
            CHECK(gpudt_f32_sub(a, b) == bits(x), "sub %08x %08x", a, b);
        }
        if (failures - before > 10)
            break;
    }
    for (uint32_t v = 0; v < 100000; v++)
        CHECK(gpudt_f32_from_u32(v) == bits((float)v), "from_u32 %u", v);
    CHECK(gpudt_f32_from_u32(0xffffffffu) == bits((float)0xffffffffu), "from_u32 max");
    printf("selftest: soft-float against the host FPU (2,000,000 random operand pairs x 4 ops): %s\n",
           failures == before ? "ok" : "FAILED");
}

static void selftest_leakage(void)
{
    int before = failures, fused_differs = 0;

    printf("selftest: leakage coefficients by core count (bit patterns)\n");
    printf("  cores  core-leak  sram-leak  host-float  fused-multiply-add\n");
    for (uint32_t n = 0; n <= 8; n++) {
        uint32_t c = gpudt_adjust_leakage(0x447a0000, n, 8, 0x3df5c28f);
        uint32_t s = gpudt_adjust_leakage(0x42340000, n, 8, 0x3e4ccccd);
        uint32_t hc = bits(m1n1_adjust_leakage(1000.0f, n, 8, 0.12f));
        uint32_t hs = bits(m1n1_adjust_leakage(45.0f, n, 8, 0.2f));
        uint32_t fc = bits(m1n1_adjust_leakage_fma(1000.0f, n, 8, 0.12f));
        uint32_t fs = bits(m1n1_adjust_leakage_fma(45.0f, n, 8, 0.2f));
        bool fd = fc != c || fs != s;

        CHECK(c == hc && s == hs, "leakage for %u cores: %08x %08x, host %08x %08x", n, c, s, hc, hs);
        fused_differs += fd;
        printf("  %5u  %08x   %08x   %s       %s\n", n, c, s, (c == hc && s == hs) ? "same" : "DIFF",
               fd ? "differs" : "same");
    }
    printf("selftest: leakage against m1n1's float code: %s; core counts where a fused multiply-add "
           "would change a bit: %d\n",
           failures == before ? "ok" : "FAILED", fused_differs);
}

static int build(const struct filter *f, const struct gpudt_config *cfg, struct gpudt_tree *t)
{
    struct gpudt_adt_ops ops = {(void *)f, fixture_getprop};

    return gpudt_build_t8103(&ops, cfg, t);
}

static void selftest_refusals(void)
{
    static struct gpudt_tree t;
    static const unsigned char six[4] = {6, 0, 0, 0}, two[4] = {2, 0, 0, 0}, zero8[8] = {0};
    static const unsigned char unterminated[4] = {'i', 'B', 'o', 'o'};
    static const unsigned char other_iboot[] = "iBoot-0.0.0";
    static const unsigned char iboot_12_3_1[] = "iBoot-7459.101.3";
    static const unsigned char iboot_13_6_1[] = "iBoot-8422.141.2.700.1";
    static const unsigned char iboot_14_0[] = "iBoot-10151.1.1";
    struct gpudt_config cfg = {.chip_id = GPUDT_CHIP_T8103, .have_core_mask = true, .core_mask = 0xff,
                               .phandle_base = 0x100};
    struct gpudt_config c2;
    struct filter f;
    int before = failures;

    CHECK(build(NULL, &cfg, &t) == GPUDT_OK, "baseline build");

    c2 = cfg, c2.chip_id = 0x8112;
    CHECK(build(NULL, &c2, &t) == GPUDT_ERR_CHIP, "wrong chip is refused");
    c2 = cfg, c2.have_core_mask = false;
    CHECK(build(NULL, &c2, &t) == GPUDT_ERR_NEED_CORE_MASK, "missing core mask is refused");
    c2.assume_all_cores = true;
    CHECK(build(NULL, &c2, &t) == GPUDT_OK && t.core_count == 8 && t.ndiags > 0, "assume_all_cores");

    f = (struct filter){.hide_path = "/arm-io/sgx", .hide_name = "perf-state-count"};
    CHECK(build(&f, &cfg, &t) == GPUDT_ERR_NO_SGX, "no perf-state-count");
    f = (struct filter){.hide_path = "/arm-io/sgx", .hide_name = "perf-states"};
    CHECK(build(&f, &cfg, &t) == GPUDT_ERR_PERF_STATES, "no perf-states");
    f = (struct filter){.patch_name = "perf-state-count", .patch_value = six, .patch_len = 4};
    CHECK(build(&f, &cfg, &t) == GPUDT_ERR_PERF_STATE_COUNT, "6 perf states");
    f = (struct filter){.patch_name = "perf-state-table-count", .patch_value = two, .patch_len = 4};
    CHECK(build(&f, &cfg, &t) == GPUDT_ERR_PERF_STATE_COUNT, "2 perf state tables");
    f = (struct filter){.patch_name = "perf-states", .patch_value = zero8, .patch_len = 8};
    CHECK(build(&f, &cfg, &t) == GPUDT_ERR_PERF_STATES, "short perf-states");
    f = (struct filter){.hide_path = "/arm-io/sgx", .hide_name = "gfx-handoff-base"};
    CHECK(build(&f, &cfg, &t) == GPUDT_ERR_REGION, "no gfx-handoff-base");
    f = (struct filter){.patch_name = "gpu-region-size", .patch_value = zero8, .patch_len = 8};
    CHECK(build(&f, &cfg, &t) == GPUDT_ERR_REGION, "zero gpu-region-size");
    f = (struct filter){.hide_path = "/chosen", .hide_name = "firmware-version"};
    CHECK(build(&f, &cfg, &t) == GPUDT_ERR_NO_FIRMWARE, "no firmware-version");

    /* firmware table: the compat mapping and the unknown case, through a patched /chosen */
    {
        struct { const unsigned char *s; unsigned len; uint32_t ver[3], compat[3]; int ret; } cases[] = {
            {iboot_12_3_1, sizeof(iboot_12_3_1), {12, 3, 1}, {12, 3, 0}, GPUDT_OK},
            {iboot_13_6_1, sizeof(iboot_13_6_1), {13, 6, 1}, {13, 5, 0}, GPUDT_OK},
            {iboot_14_0, sizeof(iboot_14_0), {14, 0, 0}, {14, 0, 0}, GPUDT_OK},
            {other_iboot, sizeof(other_iboot), {0, 0, 0}, {0, 0, 0}, GPUDT_OK},
            {unterminated, sizeof(unterminated), {0, 0, 0}, {0, 0, 0}, GPUDT_ERR_NO_FIRMWARE},
        };
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            /* /chosen firmware-version from the case, everything else from the fixture */
            struct chosen_ctx cc = {cases[i].s, cases[i].len};
            struct gpudt_adt_ops ops = {&cc, chosen_getprop};
            int r = gpudt_build_t8103(&ops, &cfg, &t);

            CHECK(r == cases[i].ret, "firmware case %u returns %d", i, r);
            if (r == GPUDT_OK)
                CHECK(!memcmp(t.fw_version, cases[i].ver, 12) && !memcmp(t.fw_compat, cases[i].compat, 12),
                      "firmware case %u tuples", i);
        }
    }
    printf("selftest: refusal paths and firmware mapping: %s\n", failures == before ? "ok" : "FAILED");
}

static uint64_t be64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = v << 8 | p[i];
    return v;
}

/* every UAT region must be exactly one of iBoot's carve-outs, i.e. memory XNU is never given */
static void selftest_regions(void)
{
    static struct gpudt_tree t;
    static const int nodes[] = {GPUDT_NODE_UAT_HANDOFF, GPUDT_NODE_UAT_PAGETABLES, GPUDT_NODE_UAT_TTBS};
    struct gpudt_config cfg = {.chip_id = GPUDT_CHIP_T8103, .have_core_mask = true, .core_mask = 0xff,
                               .phandle_base = 0x100};
    int before = failures;

    CHECK(build(NULL, &cfg, &t) == GPUDT_OK, "build");
    for (unsigned i = 0; i < 3; i++) {
        const struct gpudt_prop *reg = gpudt_find(&t, nodes[i], "reg");
        bool found = false;

        CHECK(reg && reg->len == 16, "%s has a reg", t.nodes[nodes[i]].name);
        if (!reg)
            continue;
        for (unsigned j = 0; j < fixture_nprops; j++) {
            const struct fixture_prop *f = &fixture_props[j];
            uint64_t a, s;
            if (strcmp(f->path, "/chosen/carveout-memory-map") || f->len != 16)
                continue;
            memcpy(&a, f->value, 8); /* little-endian host assumed, as for the saved tree */
            memcpy(&s, f->value + 8, 8);
            if (a == be64(reg->value) && s == be64(reg->value + 8))
                found = true;
        }
        CHECK(found, "%s is not one of iBoot's carve-outs", t.nodes[nodes[i]].name);
    }
    printf("selftest: the three UAT regions are each exactly one iBoot carve-out: %s\n",
           failures == before ? "ok" : "FAILED");
}

/*
 * With change 1 of kernel/sf_linux_of.patch, text is what the node lists and everything else
 * takes the numeric path. That path returns the FDT bytes for every value here provided no text
 * property has a name the layer forces numeric, every other value is whole cells, and no 8-byte
 * value has a name the layer treats as a single u64.
 */
static void selftest_patched_of_layer(void)
{
    static struct gpudt_tree t;
    static const char *const forced[] = {"reg", "ranges", "dma-ranges", "phandle", "linux,phandle",
                                         "interrupt-parent", "interrupts", "interrupts-extended"};
    struct gpudt_config cfg = {.chip_id = GPUDT_CHIP_T8103, .have_core_mask = true, .core_mask = 0xff,
                               .phandle_base = 0x100, .mbox_phandle = 1, .power_domain_phandle = 2};
    int before = failures, misread = 0;

    CHECK(build(NULL, &cfg, &t) == GPUDT_OK, "build");
    for (uint32_t i = 0; i < t.nprops; i++) {
        const struct gpudt_prop *p = &t.props[i];
        size_t n = strlen(p->name);

        misread += !gpudt_prop_iodt_roundtrips(p);
        if (p->type == GPUDT_T_STRINGS) {
            for (unsigned j = 0; j < sizeof(forced) / sizeof(forced[0]); j++)
                CHECK(strcmp(p->name, forced[j]) && p->name[0] != '#', "text property %s", p->name);
            continue;
        }
        CHECK(p->len % 4 == 0, "%s is not whole cells", p->name);
        if (p->type == GPUDT_T_REG)
            CHECK(!strcmp(p->name, "reg") && p->len % 16 == 0, "reg layout of %s", p->name);
        else
            CHECK(strcmp(p->name, "reg"), "cells property named reg");
        CHECK(!(p->len == 8 && ((n >= 10 && !strcmp(p->name + n - 10, "-frequency")) ||
                                !strncmp(p->name, "linux,initrd-", 13))),
              "%s would be read as one u64", p->name);
    }
    printf("selftest: sf_linux_of.c as it is misreads %d value(s); with the proposed string list: %s\n",
           misread, failures == before ? "none" : "FAILED");
}

static const char *const type_names[] = {"empty", "strings", "cells", "reg"};
static const char *const origin_names[] = {"static", "adt", "adt+hw", "firmware", "synth"};
static const char *const xcheck_names[] = {"-", "match", "MISMATCH", "ABSENT"};

static void emit_line(void *ctx, const char *line)
{
    printf("%s%s\n", (const char *)ctx, line);
}

/* what kernel/SFGpuDT.cpp logs: same library, same configuration, same formatter */
static int kextlog(struct gpudt_config cfg)
{
    static struct gpudt_tree t;
    int ret;

    cfg.phandle_base = GPUDT_DARWIN_PHANDLE_BASE;
    cfg.mbox_phandle = 0;
    cfg.power_domain_phandle = 0;
    ret = build(NULL, &cfg, &t);
    if (ret != GPUDT_OK) {
        printf("SFGpuDT: the device tree was refused: %d\n", ret);
        return 1;
    }
    for (uint32_t i = 0; i < t.ndiags; i++)
        printf("SFGpuDT: note %u %s (0x%llx, 0x%llx)\n", t.diags[i].code, t.diags[i].text ? t.diags[i].text : "",
               (unsigned long long)t.diags[i].a, (unsigned long long)t.diags[i].b);
    gpudt_summary(&t, emit_line, "SFGpuDT: ");
    return 0;
}

static int dump(const struct gpudt_config *cfg)
{
    static struct gpudt_tree t;
    char path[128];
    int ret = build(NULL, cfg, &t);

    printf("R|%d\n", ret);
    if (ret != GPUDT_OK)
        return 1;
    printf("I|cores|%u\n", t.core_count);
    printf("I|firmware|%s\n", t.fw_string);
    {
        uint32_t n, h = gpudt_checksum(&t, GPUDT_CHECKSUM_LINUX, -1, &n);
        printf("I|linux_checksum|%08x\n", h);
        printf("I|linux_count|%u\n", n);
    }
    for (uint32_t i = 0; i < t.ndiags; i++)
        printf("D|%u|%s|%llu|%llu\n", t.diags[i].code, t.diags[i].text ? t.diags[i].text : "",
               (unsigned long long)t.diags[i].a, (unsigned long long)t.diags[i].b);
    for (uint32_t i = 0; i < t.nprops; i++) {
        const struct gpudt_prop *p = &t.props[i];
        uint8_t iodt[GPUDT_MAX_VALUE];

        gpudt_prop_to_iodt(p, iodt);
        printf("P|%s|%s|%s|%s|%u|%s|%d|%u|", gpudt_node_path(&t, p->node, path, sizeof(path)), p->name,
               type_names[p->type], origin_names[p->origin], p->flags, xcheck_names[p->xcheck],
               gpudt_prop_iodt_roundtrips(p), p->len);
        for (unsigned j = 0; j < p->len; j++)
            printf("%02x", p->value[j]);
        printf("|");
        for (unsigned j = 0; j < p->len; j++)
            printf("%02x", iodt[j]);
        printf("\n");
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct gpudt_config cfg = {.chip_id = GPUDT_CHIP_T8103, .phandle_base = 0x100};

    if (argc >= 2 && !strcmp(argv[1], "selftest")) {
        selftest_softfloat();
        selftest_leakage();
        selftest_refusals();
        selftest_regions();
        selftest_patched_of_layer();
        printf("selftest: %s\n", failures ? "FAILED" : "all ok");
        return failures ? 1 : 0;
    }
    if (argc >= 2 && (!strcmp(argv[1], "dump") || !strcmp(argv[1], "kextlog"))) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "--assume-all-cores")) {
                cfg.assume_all_cores = true;
            } else if (i + 1 < argc && !strcmp(argv[i], "--phandle-base")) {
                cfg.phandle_base = (uint32_t)strtoul(argv[++i], NULL, 0);
            } else if (i + 1 < argc && !strcmp(argv[i], "--mbox")) {
                cfg.mbox_phandle = (uint32_t)strtoul(argv[++i], NULL, 0);
            } else if (i + 1 < argc && !strcmp(argv[i], "--power")) {
                cfg.power_domain_phandle = (uint32_t)strtoul(argv[++i], NULL, 0);
            } else if (i + 1 < argc && !strcmp(argv[i], "--core-mask")) {
                cfg.have_core_mask = true;
                cfg.core_mask = (uint32_t)strtoul(argv[++i], NULL, 0);
            } else {
                fprintf(stderr, "unknown argument %s\n", argv[i]);
                return 2;
            }
        }
        return !strcmp(argv[1], "kextlog") ? kextlog(cfg) : dump(&cfg);
    }
    fprintf(stderr, "usage: harness dump [options] | harness kextlog [options] | harness selftest\n");
    return 2;
}
