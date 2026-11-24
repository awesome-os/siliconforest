/* SPDX-License-Identifier: MIT */
/*
 * m1-gpu-dt: Linux-style device-tree properties for the Apple T8103 (M1, G13G)
 * GPU, synthesised from Apple's device tree (ADT).
 *
 * Ported from m1n1 (MIT, Copyright The Asahi Linux Contributors):
 *   src/kboot_gpu.c  dt_set_gpu() for chip T8103, calc_power_t8103(),
 *                    get_core_counts(), adjust_leakage(), dt_set_region(),
 *                    dt_set_resvmem(), fdt_set_float_array()
 *   src/firmware.c   fw_versions[], detect_firmware(), firmware_init(),
 *                    firmware_set_fdt()
 *   src/kboot.c      the /chosen asahi,*-version strings
 * m1n1 patches a compiled Linux device tree in place; this file produces the
 * same properties from nothing, so the static part of that device tree
 * (arch/arm64/boot/dts/apple/t8103.dtsi, GPL-2.0+ OR MIT) is reproduced here
 * as a table.
 *
 * The to_fdt() model in gpudt_prop_iodt_roundtrips() follows SiliconForest's
 * fbsdkpi/sf/lkpi/sf_linux_of.c (MIT).
 */
#include "gpudt.h"

#include <string.h>

/* ---- little helpers ---------------------------------------------------- */

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t rd64le(const uint8_t *p)
{
    return (uint64_t)rd32le(p) | (uint64_t)rd32le(p + 4) << 32;
}

static uint32_t rd32be(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static void wr32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void diag(struct gpudt_tree *t, enum gpudt_diag_code code, const char *text, uint64_t a,
                 uint64_t b)
{
    if (t->ndiags >= GPUDT_MAX_DIAGS)
        return;
    t->diags[t->ndiags].code = (uint16_t)code;
    t->diags[t->ndiags].text = text;
    t->diags[t->ndiags].a = a;
    t->diags[t->ndiags].b = b;
    t->ndiags++;
}

static struct gpudt_prop *add_raw(struct gpudt_tree *t, int node, const char *name, int type,
                                  int origin, unsigned flags, const void *bytes, size_t len)
{
    struct gpudt_prop *p;

    if (len > GPUDT_MAX_VALUE) {
        diag(t, GPUDT_D_VALUE_TOO_LONG, name, len, 0);
        return NULL;
    }
    if (t->nprops >= GPUDT_MAX_PROPS)
        return NULL;
    p = &t->props[t->nprops++];
    p->node = (uint8_t)node;
    p->type = (uint8_t)type;
    p->origin = (uint8_t)origin;
    p->xcheck = GPUDT_X_NONE;
    p->flags = (uint8_t)flags;
    p->len = (uint8_t)len;
    p->name = name;
    if (len)
        memcpy(p->value, bytes, len);
    return p;
}

static struct gpudt_prop *add_cells(struct gpudt_tree *t, int node, const char *name, int origin,
                                    unsigned flags, const uint32_t *cells, size_t n)
{
    uint8_t buf[GPUDT_MAX_VALUE];

    if (n * 4 > sizeof(buf)) {
        diag(t, GPUDT_D_VALUE_TOO_LONG, name, n * 4, 0);
        return NULL;
    }
    for (size_t i = 0; i < n; i++)
        wr32be(buf + 4 * i, cells[i]);
    return add_raw(t, node, name, GPUDT_T_CELLS, origin, flags, buf, n * 4);
}

static struct gpudt_prop *add_u32(struct gpudt_tree *t, int node, const char *name, int origin,
                                  unsigned flags, uint32_t v)
{
    return add_cells(t, node, name, origin, flags, &v, 1);
}

static struct gpudt_prop *add_str(struct gpudt_tree *t, int node, const char *name, int origin,
                                  unsigned flags, const char *s)
{
    return add_raw(t, node, name, GPUDT_T_STRINGS, origin, flags, s, strlen(s) + 1);
}

/* reg = <addr size> with #address-cells = #size-cells = 2, n tuples */
static struct gpudt_prop *add_reg(struct gpudt_tree *t, int node, int origin, unsigned flags,
                                  const uint64_t *addr_size, size_t n)
{
    uint8_t buf[GPUDT_MAX_VALUE];

    if (n * 16 > sizeof(buf))
        return NULL;
    for (size_t i = 0; i < 2 * n; i++) {
        wr32be(buf + 8 * i, (uint32_t)(addr_size[i] >> 32));
        wr32be(buf + 8 * i + 4, (uint32_t)addr_size[i]);
    }
    return add_raw(t, node, "reg", GPUDT_T_REG, origin, flags, buf, n * 16);
}

static const uint8_t *adt_get(const struct gpudt_adt_ops *adt, const char *path, const char *name,
                              uint32_t *len)
{
    uint32_t l = 0;
    const void *p = adt->getprop(adt->ctx, path, name, &l);

    if (len)
        *len = p ? l : 0;
    return p;
}

static bool adt_u32(const struct gpudt_adt_ops *adt, const char *path, const char *name,
                    uint32_t *out)
{
    uint32_t len;
    const uint8_t *p = adt_get(adt, path, name, &len);

    /* m1n1 ADT_GETPROP: the property must have exactly the size of the variable */
    if (!p || len != 4)
        return false;
    *out = rd32le(p);
    return true;
}

static bool adt_u64(const struct gpudt_adt_ops *adt, const char *path, const char *name,
                    uint64_t *out)
{
    uint32_t len;
    const uint8_t *p = adt_get(adt, path, name, &len);

    if (!p || len != 8)
        return false;
    *out = rd64le(p);
    return true;
}

/* ---- IEEE-754 single precision without the FPU -------------------------- */

/* value = m * 2^e, m != 0 */
static uint32_t f32_pack(uint64_t m, int e, bool sticky)
{
    int msb = 63;

    if (m == 0)
        return 0;
    while (!(m >> msb))
        msb--;
    if (msb > 23) {
        int s = msb - 23;
        uint64_t rest = m & ((1ull << s) - 1);
        uint64_t half = 1ull << (s - 1);

        m >>= s;
        e += s;
        if (rest > half || (rest == half && (sticky || (m & 1))))
            m++;
        if (m >> 24) {
            m >>= 1;
            e++;
        }
    } else {
        m <<= 23 - msb;
        e -= 23 - msb;
    }
    return (uint32_t)(e + 23 + 127) << 23 | (uint32_t)(m & 0x7fffff);
}

static uint64_t f32_unpack(uint32_t bits, int *e)
{
    if (!(bits & 0x7fffffff)) {
        *e = 0;
        return 0;
    }
    *e = (int)((bits >> 23) & 0xff) - 127 - 23;
    return (bits & 0x7fffff) | 0x800000;
}

uint32_t gpudt_f32_from_u32(uint32_t v)
{
    return f32_pack(v, 0, false);
}

uint32_t gpudt_f32_mul(uint32_t a, uint32_t b)
{
    int ea, eb;
    uint64_t ma = f32_unpack(a, &ea), mb = f32_unpack(b, &eb);

    return f32_pack(ma * mb, ea + eb, false);
}

uint32_t gpudt_f32_div(uint32_t a, uint32_t b)
{
    int ea, eb;
    uint64_t ma = f32_unpack(a, &ea), mb = f32_unpack(b, &eb);

    if (!ma || !mb)
        return 0;
    ma <<= 39;
    return f32_pack(ma / mb, ea - eb - 39, (ma % mb) != 0);
}

static uint32_t f32_addsub(uint32_t a, uint32_t b, bool sub)
{
    int ea, eb, d;
    uint64_t ma = f32_unpack(a, &ea), mb = f32_unpack(b, &eb);

    if (!mb)
        return a;
    if (!ma)
        return sub ? 0 : b;
    if (ea < eb) {
        /* only reached for add: sub requires a >= b */
        uint64_t tm = ma;
        int te = ea;
        uint32_t tb = a;
        ma = mb, ea = eb, a = b;
        mb = tm, eb = te, b = tb;
    }
    d = ea - eb;
    if (d > 38)
        return a; /* b is far below half an ulp of a */
    ma <<= d;
    return f32_pack(sub ? ma - mb : ma + mb, eb, false);
}

uint32_t gpudt_f32_add(uint32_t a, uint32_t b)
{
    return f32_addsub(a, b, false);
}

uint32_t gpudt_f32_sub(uint32_t a, uint32_t b)
{
    return f32_addsub(a, b, true);
}

/*
 * m1n1 adjust_leakage():
 *     float uncore = val * uncore_fraction;
 *     float core = val - uncore;
 *     val = uncore + (cores / (float)max) * core;
 * evaluated here step by step, each step rounded to single precision.
 */
uint32_t gpudt_adjust_leakage(uint32_t val, uint32_t cores, uint32_t max_cores,
                              uint32_t uncore_fraction)
{
    uint32_t uncore = gpudt_f32_mul(val, uncore_fraction);
    uint32_t core = gpudt_f32_sub(val, uncore);
    uint32_t ratio = gpudt_f32_div(gpudt_f32_from_u32(cores), gpudt_f32_from_u32(max_cores));

    return gpudt_f32_add(uncore, gpudt_f32_mul(ratio, core));
}

/* ---- firmware versions (m1n1 firmware.c) -------------------------------- */

struct fw_entry {
    const char *iboot;
    const char *string;
    uint32_t num[4];
    uint32_t num_length;
};

static const struct fw_entry fw_versions[] = {
#include "gpudt_fw_table.inc"
};

static const struct fw_entry fw_unknown = {"unknown", "unknown", {0, 0, 0}, 3};

/* detect_firmware(): first entry whose iBoot string is equal */
static const struct fw_entry *detect_firmware(const char *ver)
{
    for (size_t i = 0; i < sizeof(fw_versions) / sizeof(fw_versions[0]); i++)
        if (!strcmp(fw_versions[i].iboot, ver))
            return &fw_versions[i];
    return NULL;
}

static const struct fw_entry *fw_by_string(const char *s)
{
    for (size_t i = 0; i < sizeof(fw_versions) / sizeof(fw_versions[0]); i++)
        if (!strcmp(fw_versions[i].string, s))
            return &fw_versions[i];
    return &fw_unknown;
}

/*
 * dt_set_gpu(): the firmware ABI the driver is told to speak.
 *     V12_3_1            -> V12_3
 *     V13_5B4, V13_6_1   -> V13_5
 *     anything else      -> the OS firmware itself
 */
static const struct fw_entry *fw_compat(const struct fw_entry *os)
{
    if (!strcmp(os->string, "12.3.1"))
        return fw_by_string("12.3");
    if (!strcmp(os->string, "13.5 beta4") || !strcmp(os->string, "13.6.1"))
        return fw_by_string("13.5");
    return os;
}

/* ---- the static device tree (t8103.dtsi) -------------------------------- */

#define ASC_BASE 0x206400000ull
#define ASC_SIZE 0x40000ull
#define SGX_BASE 0x204000000ull
#define SGX_SIZE 0x1000000ull

struct static_u32 {
    const char *name;  /* Linux property */
    uint32_t value;    /* u32, or the bit pattern of an f32 */
    const char *adt;   /* /arm-io/sgx property carrying the same quantity, or NULL */
};

/* Order as in t8103.dtsi. f32 values are written as their bit patterns. */
static const struct static_u32 gpu_static[] = {
    {"apple,perf-base-pstate", 1, NULL},
    {"apple,min-sram-microvolt", 850000, NULL},
    {"apple,avg-power-filter-tc-ms", 1000, "gpu-avg-power-filter-tc-ms"},
    {"apple,avg-power-ki-only", 0x40f00000 /* 7.5 */, "gpu-avg-power-ki-only"},
    {"apple,avg-power-kp", 0x40800000 /* 4.0 */, "gpu-avg-power-kp"},
    {"apple,avg-power-min-duty-cycle", 40, "gpu-avg-power-min-duty-cycle"},
    {"apple,avg-power-target-filter-tc", 125, "gpu-avg-power-target-filter-tc"},
    {"apple,fast-die0-integral-gain", 0x43480000 /* 200.0 */, "gpu-fast-die0-integral-gain"},
    {"apple,fast-die0-proportional-gain", 0x40a00000 /* 5.0 */, "gpu-fast-die0-proportional-gain"},
    {"apple,perf-filter-drop-threshold", 0, "gpu-perf-filter-drop-threshold"},
    {"apple,perf-filter-time-constant", 5, "gpu-perf-filter-time-constant"},
    {"apple,perf-filter-time-constant2", 50, "gpu-perf-filter-time-constant2"},
    {"apple,perf-integral-gain2", 0x3e4a2121 /* 0.197392 */, "gpu-perf-integral-gain2"},
    {"apple,perf-integral-min-clamp", 0, "gpu-perf-integral-min-clamp"},
    {"apple,perf-proportional-gain2", 0x40db53d0 /* 6.853981 */, "gpu-perf-proportional-gain2"},
    {"apple,perf-tgt-utilization", 85, "gpu-perf-tgt-utilization"},
    {"apple,power-sample-period", 8, "gpu-power-sample-period"},
    /* apple,power-zones goes here, see below */
    {"apple,ppm-filter-time-constant-ms", 100, "gpu-ppm-filter-time-constant-ms"},
    {"apple,ppm-ki", 0x42b70000 /* 91.5 */, "gpu-ppm-ki"},
    {"apple,ppm-kp", 0x40dccccd /* 6.9 */, "gpu-ppm-kp"},
    {"apple,pwr-filter-time-constant", 313, "gpu-pwr-filter-time-constant"},
    {"apple,pwr-integral-gain", 0x3ca59586 /* 0.0202129 */, "gpu-pwr-integral-gain"},
    {"apple,pwr-integral-min-clamp", 0, "gpu-pwr-integral-min-clamp"},
    {"apple,pwr-min-duty-cycle", 40, "gpu-pwr-min-duty-cycle"},
    {"apple,pwr-proportional-gain", 0x40a90fdb /* 5.2831855 */, "gpu-pwr-proportional-gain"},
};
#define STATIC_POWER_ZONES_AFTER "apple,power-sample-period"

static const uint32_t power_zones_static[3] = {30000, 100, 6875};
static const char *const power_zones_adt[3] = {
    "gpu-power-zone-target-0", "gpu-power-zone-target-offset-0", "gpu-power-zone-filter-tc-0"};

/* calc_power_t8103(): max power of state i = voltage [mV] * scale[i] * 100 [uW] */
static const uint32_t t8103_pwr_scale[GPUDT_T8103_NUM_OPPS] = {0, 63, 80, 108, 150, 198, 210};

static const char *const opp_names[GPUDT_T8103_NUM_OPPS] = {"opp00", "opp01", "opp02", "opp03",
                                                            "opp04", "opp05", "opp06"};

#define SGX "/arm-io/sgx"

static void set_node(struct gpudt_tree *t, int id, const char *name, int parent, uint32_t phandle)
{
    t->nodes[id].name = name;
    t->nodes[id].parent = (int8_t)parent;
    t->nodes[id].present = true;
    t->nodes[id].phandle = phandle;
}

/* An ADT address below /arm-io, through /arm-io ranges (child, parent, size as u64 each). */
static bool armio_translate(const struct gpudt_adt_ops *adt, uint64_t *addr)
{
    uint32_t len;
    const uint8_t *r = adt_get(adt, "/arm-io", "ranges", &len);

    if (!r)
        return false;
    for (uint32_t off = 0; off + 24 <= len; off += 24) {
        uint64_t child = rd64le(r + off), parent = rd64le(r + off + 8), size = rd64le(r + off + 16);
        if (*addr >= child && *addr - child < size) {
            *addr = *addr - child + parent;
            return true;
        }
    }
    return false;
}

static void check_reg(struct gpudt_tree *t, const struct gpudt_adt_ops *adt, const char *path,
                      const char *what, uint64_t expect)
{
    uint32_t len;
    const uint8_t *reg = adt_get(adt, path, "reg", &len);
    uint64_t addr;

    if (!reg || len < 16) {
        diag(t, GPUDT_D_STATIC_ADT_ABSENT, what, 0, 0);
        return;
    }
    addr = rd64le(reg);
    if (!armio_translate(adt, &addr) || addr != expect)
        diag(t, GPUDT_D_REG_ADT_MISMATCH, what, expect, addr);
}

/* dt_set_region() + dt_set_resvmem(): reg and status of one UAT region, straight from the ADT */
static int set_region(struct gpudt_tree *t, const struct gpudt_adt_ops *adt, int node,
                      const char *base_prop, const char *size_prop, uint64_t min_size)
{
    uint64_t as[2];

    if (!adt_u64(adt, SGX, base_prop, &as[0]) || !as[0])
        return GPUDT_ERR_REGION;
    if (!adt_u64(adt, SGX, size_prop, &as[1]) || !as[1])
        return GPUDT_ERR_REGION;
    if (as[1] < min_size)
        diag(t, GPUDT_D_REGION_TOO_SMALL, t->nodes[node].name, as[1], min_size);
    if (!add_reg(t, node, GPUDT_O_ADT, GPUDT_F_DRIVER_READS, as, 1) ||
        !add_str(t, node, "status", GPUDT_O_ADT, GPUDT_F_FRAMEWORK, "okay") ||
        !add_u32(t, node, "phandle", GPUDT_O_SYNTH, GPUDT_F_PHANDLE | GPUDT_F_FRAMEWORK,
                 t->nodes[node].phandle))
        return GPUDT_ERR_OVERFLOW;
    return GPUDT_OK;
}

/* A region m1n1 allocates and fills at boot; left as the device tree source has it. */
static int set_disabled_region(struct gpudt_tree *t, int node)
{
    if (!add_str(t, node, "status", GPUDT_O_STATIC, GPUDT_F_FRAMEWORK | GPUDT_F_RUNTIME, "disabled") ||
        !add_u32(t, node, "phandle", GPUDT_O_SYNTH, GPUDT_F_PHANDLE | GPUDT_F_FRAMEWORK,
                 t->nodes[node].phandle))
        return GPUDT_ERR_OVERFLOW;
    return GPUDT_OK;
}

static int add_static_u32(struct gpudt_tree *t, const struct gpudt_adt_ops *adt,
                          const struct static_u32 *s)
{
    struct gpudt_prop *p =
        add_u32(t, GPUDT_NODE_GPU, s->name, GPUDT_O_STATIC, GPUDT_F_DRIVER_READS, s->value);
    uint32_t v;

    if (!p)
        return GPUDT_ERR_OVERFLOW;
    if (!s->adt)
        return GPUDT_OK;
    if (!adt_u32(adt, SGX, s->adt, &v)) {
        p->xcheck = GPUDT_X_ABSENT;
        diag(t, GPUDT_D_STATIC_ADT_ABSENT, s->name, 0, 0);
    } else if (v != s->value) {
        p->xcheck = GPUDT_X_MISMATCH;
        diag(t, GPUDT_D_STATIC_ADT_MISMATCH, s->name, s->value, v);
    } else {
        p->xcheck = GPUDT_X_MATCH;
    }
    return GPUDT_OK;
}

static int add_power_zones(struct gpudt_tree *t, const struct gpudt_adt_ops *adt)
{
    struct gpudt_prop *p = add_cells(t, GPUDT_NODE_GPU, "apple,power-zones", GPUDT_O_STATIC,
                                     GPUDT_F_DRIVER_READS, power_zones_static, 3);

    if (!p)
        return GPUDT_ERR_OVERFLOW;
    p->xcheck = GPUDT_X_MATCH;
    for (int i = 0; i < 3; i++) {
        uint32_t v;
        if (!adt_u32(adt, SGX, power_zones_adt[i], &v)) {
            p->xcheck = GPUDT_X_ABSENT;
            diag(t, GPUDT_D_STATIC_ADT_ABSENT, "apple,power-zones", 0, 0);
            break;
        }
        if (v != power_zones_static[i]) {
            p->xcheck = GPUDT_X_MISMATCH;
            diag(t, GPUDT_D_STATIC_ADT_MISMATCH, "apple,power-zones", power_zones_static[i], v);
        }
    }
    return GPUDT_OK;
}

static const char *terminated_string(const uint8_t *p, uint32_t len)
{
    /* firmware_init(): p && len && p[len - 1] == 0 */
    return (p && len && p[len - 1] == 0) ? (const char *)p : NULL;
}

int gpudt_build_t8103(const struct gpudt_adt_ops *adt, const struct gpudt_config *cfg,
                      struct gpudt_tree *t)
{
    const unsigned DRV = GPUDT_F_DRIVER_READS;
    uint32_t ph, len, perf_state_count, perf_state_table_count, core_mask, max_pwr[GPUDT_T8103_NUM_OPPS];
    const uint8_t *perf_states, *p;
    const struct fw_entry *os_fw, *sys_fw, *compat;
    const char *os_iboot, *sys_iboot;
    int ret;

    if (!adt || !adt->getprop || !cfg || !t)
        return GPUDT_ERR_ARGS;
    memset(t, 0, sizeof(*t));
    if (cfg->chip_id != GPUDT_CHIP_T8103)
        return GPUDT_ERR_CHIP;

    ph = cfg->phandle_base;
    set_node(t, GPUDT_NODE_GPU, "gpu@206400000", -1, 0);
    set_node(t, GPUDT_NODE_RESERVED_MEMORY, "reserved-memory", -1, 0);
    set_node(t, GPUDT_NODE_UAT_TTBS, "uat-ttbs@30", GPUDT_NODE_RESERVED_MEMORY, ph + 0);
    set_node(t, GPUDT_NODE_UAT_PAGETABLES, "uat-pagetables@20", GPUDT_NODE_RESERVED_MEMORY, ph + 1);
    set_node(t, GPUDT_NODE_UAT_HANDOFF, "uat-handoff@10", GPUDT_NODE_RESERVED_MEMORY, ph + 2);
    set_node(t, GPUDT_NODE_HW_CAL_A, "hw-cal-a", GPUDT_NODE_RESERVED_MEMORY, ph + 3);
    set_node(t, GPUDT_NODE_HW_CAL_B, "hw-cal-b", GPUDT_NODE_RESERVED_MEMORY, ph + 4);
    set_node(t, GPUDT_NODE_GLOBALS, "globals", GPUDT_NODE_RESERVED_MEMORY, ph + 5);
    set_node(t, GPUDT_NODE_OPP_TABLE, "opp-table-gpu", -1, ph + 6);
    for (int i = 0; i < GPUDT_T8103_NUM_OPPS; i++)
        set_node(t, GPUDT_NODE_OPP0 + i, opp_names[i], GPUDT_NODE_OPP_TABLE, 0);
    set_node(t, GPUDT_NODE_CHOSEN, "chosen", -1, 0);

    /* ---- ADT inputs, validated as dt_set_gpu() does -------------------- */

    if (!adt_u32(adt, SGX, "perf-state-count", &perf_state_count) || !perf_state_count)
        return GPUDT_ERR_NO_SGX;
    if (!adt_u32(adt, SGX, "perf-state-table-count", &perf_state_table_count) ||
        !perf_state_table_count)
        return GPUDT_ERR_NO_SGX;
    /* calc_power_t8103(): exactly one table of exactly ARRAY_SIZE(t8103_pwr_scale) states */
    if (perf_state_table_count != 1 || perf_state_count != GPUDT_T8103_NUM_OPPS)
        return GPUDT_ERR_PERF_STATE_COUNT;
    /* struct perf_state { u32 freq; u32 volt; } */
    perf_states = adt_get(adt, SGX, "perf-states", &len);
    if (!perf_states || len != 8 * perf_state_count * perf_state_table_count)
        return GPUDT_ERR_PERF_STATES;

    p = adt_get(adt, "/chosen", "firmware-version", &len);
    os_iboot = terminated_string(p, len);
    if (!os_iboot)
        return GPUDT_ERR_NO_FIRMWARE;
    os_fw = detect_firmware(os_iboot);
    if (!os_fw) {
        os_fw = &fw_unknown;
        diag(t, GPUDT_D_FIRMWARE_UNKNOWN, NULL, 0, 0);
    }
    p = adt_get(adt, "/chosen", "system-firmware-version", &len);
    sys_iboot = terminated_string(p, len);
    if (sys_iboot) {
        sys_fw = detect_firmware(sys_iboot);
        if (!sys_fw)
            sys_fw = &fw_unknown;
    } else {
        sys_fw = &fw_unknown;
        sys_iboot = "unknown";
        diag(t, GPUDT_D_SYSTEM_FIRMWARE_ABSENT, NULL, 0, 0);
    }
    compat = fw_compat(os_fw);
    t->fw_string = os_fw->string;
    t->fw_version_len = os_fw->num_length;
    t->fw_compat_len = compat->num_length;
    memcpy(t->fw_version, os_fw->num, sizeof(t->fw_version));
    memcpy(t->fw_compat, compat->num, sizeof(t->fw_compat));
    /* driver.rs: G13 has managers for compat 12.3.0 and 13.5.0 only */
    if (compat->num_length != 3 || compat->num[2] != 0 ||
        !((compat->num[0] == 12 && compat->num[1] == 3) ||
          (compat->num[0] == 13 && compat->num[1] == 5)))
        diag(t, GPUDT_D_FIRMWARE_UNSUPPORTED, compat->string, compat->num[0], compat->num[1]);

    /* get_core_counts(): popcount of the low 8 bits of SGX + 0xd01500 */
    if (cfg->have_core_mask) {
        core_mask = cfg->core_mask;
    } else if (cfg->assume_all_cores) {
        core_mask = (1u << GPUDT_T8103_MAX_CORES) - 1;
        diag(t, GPUDT_D_ASSUMED_ALL_CORES, NULL, core_mask, 0);
    } else {
        return GPUDT_ERR_NEED_CORE_MASK;
    }
    t->core_mask = core_mask & ((1u << GPUDT_T8103_MAX_CORES) - 1);
    t->core_mask_assumed = !cfg->have_core_mask;
    t->core_count = 0;
    for (int i = 0; i < GPUDT_T8103_MAX_CORES; i++)
        t->core_count += (core_mask >> i) & 1;
    if (t->core_count != GPUDT_T8103_MAX_CORES)
        diag(t, GPUDT_D_REDUCED_CORES, NULL, t->core_count, 0);

    /* ---- gpu@206400000 -------------------------------------------------- */

    {
        static const char compatible[] = "apple,agx-t8103\0apple,agx-g13g";
        static const char reg_names[] = "asc\0sgx";
        static const char region_names[] = "ttbs\0pagetables\0handoff\0hw-cal-a\0hw-cal-b\0globals";
        const uint64_t reg[4] = {ASC_BASE, ASC_SIZE, SGX_BASE, SGX_SIZE};
        uint32_t regions[6];

        if (!add_raw(t, GPUDT_NODE_GPU, "compatible", GPUDT_T_STRINGS, GPUDT_O_STATIC, DRV,
                     compatible, sizeof(compatible)) ||
            !add_reg(t, GPUDT_NODE_GPU, GPUDT_O_STATIC, DRV, reg, 2) ||
            !add_raw(t, GPUDT_NODE_GPU, "reg-names", GPUDT_T_STRINGS, GPUDT_O_STATIC, DRV,
                     reg_names, sizeof(reg_names)))
            return GPUDT_ERR_OVERFLOW;
        check_reg(t, adt, "/arm-io/gfx-asc", "asc", ASC_BASE);
        check_reg(t, adt, SGX, "sgx", SGX_BASE);

        if (cfg->mbox_phandle) {
            if (!add_u32(t, GPUDT_NODE_GPU, "mboxes", GPUDT_O_SYNTH,
                         GPUDT_F_PHANDLE | GPUDT_F_FRAMEWORK, cfg->mbox_phandle))
                return GPUDT_ERR_OVERFLOW;
        } else {
            diag(t, GPUDT_D_NO_MBOX_PHANDLE, NULL, 0, 0);
        }
        if (cfg->power_domain_phandle) {
            if (!add_u32(t, GPUDT_NODE_GPU, "power-domains", GPUDT_O_SYNTH,
                         GPUDT_F_PHANDLE | GPUDT_F_FRAMEWORK, cfg->power_domain_phandle))
                return GPUDT_ERR_OVERFLOW;
        } else {
            diag(t, GPUDT_D_NO_POWER_DOMAIN_PHANDLE, NULL, 0, 0);
        }

        for (int i = 0; i < 6; i++)
            regions[i] = ph + (uint32_t)i;
        if (!add_cells(t, GPUDT_NODE_GPU, "memory-region", GPUDT_O_SYNTH, GPUDT_F_PHANDLE | DRV,
                       regions, 6) ||
            !add_raw(t, GPUDT_NODE_GPU, "memory-region-names", GPUDT_T_STRINGS, GPUDT_O_STATIC,
                     DRV, region_names, sizeof(region_names)))
            return GPUDT_ERR_OVERFLOW;

        /*
         * firmware_set_fdt(): num_length cells. apple,firmware-abi is the
         * compat tuple; the device tree source has <0 0 0> there, and m1n1
         * replaces it.
         */
        if (!add_cells(t, GPUDT_NODE_GPU, "apple,firmware-abi", GPUDT_O_FIRMWARE, 0, compat->num,
                       compat->num_length) ||
            !add_cells(t, GPUDT_NODE_GPU, "apple,firmware-version", GPUDT_O_FIRMWARE, DRV,
                       os_fw->num, os_fw->num_length) ||
            !add_cells(t, GPUDT_NODE_GPU, "apple,firmware-compat", GPUDT_O_FIRMWARE, DRV,
                       compat->num, compat->num_length))
            return GPUDT_ERR_OVERFLOW;

        if (!add_u32(t, GPUDT_NODE_GPU, "operating-points-v2", GPUDT_O_SYNTH, GPUDT_F_PHANDLE | DRV,
                     ph + 6))
            return GPUDT_ERR_OVERFLOW;
    }

    for (size_t i = 0; i < sizeof(gpu_static) / sizeof(gpu_static[0]); i++) {
        if ((ret = add_static_u32(t, adt, &gpu_static[i])))
            return ret;
        if (!strcmp(gpu_static[i].name, STATIC_POWER_ZONES_AFTER) && (ret = add_power_zones(t, adt)))
            return ret;
    }

    /*
     * calc_power_t8103(): core_leak = 1000.0, sram_leak = 45.0, then
     * adjust_leakage(core_leak, .., 0.12) and adjust_leakage(sram_leak, .., 0.2).
     */
    {
        uint32_t core_leak = gpudt_adjust_leakage(0x447a0000 /* 1000.0f */, t->core_count,
                                                  GPUDT_T8103_MAX_CORES, 0x3df5c28f /* 0.12f */);
        uint32_t sram_leak = gpudt_adjust_leakage(0x42340000 /* 45.0f */, t->core_count,
                                                  GPUDT_T8103_MAX_CORES, 0x3e4ccccd /* 0.2f */);

        if (!add_u32(t, GPUDT_NODE_GPU, "apple,core-leak-coef", GPUDT_O_ADT_HW, DRV, core_leak) ||
            !add_u32(t, GPUDT_NODE_GPU, "apple,sram-leak-coef", GPUDT_O_ADT_HW, DRV, sram_leak))
            return GPUDT_ERR_OVERFLOW;
    }

    /* ---- opp-table-gpu --------------------------------------------------- */

    if (!add_str(t, GPUDT_NODE_OPP_TABLE, "compatible", GPUDT_O_STATIC, GPUDT_F_FRAMEWORK,
                 "operating-points-v2") ||
        !add_u32(t, GPUDT_NODE_OPP_TABLE, "phandle", GPUDT_O_SYNTH,
                 GPUDT_F_PHANDLE | GPUDT_F_FRAMEWORK, ph + 6))
        return GPUDT_ERR_OVERFLOW;
    for (uint32_t i = 0; i < perf_state_count; i++) {
        uint32_t freq = rd32le(perf_states + 8 * i);
        uint32_t volt = rd32le(perf_states + 8 * i + 4);
        uint32_t hz[2] = {0, freq}; /* fdt_setprop_inplace_u64(freq) */
        uint32_t uv = volt * 1000;

        max_pwr[i] = volt * t8103_pwr_scale[i] * 100;
        if (!add_cells(t, GPUDT_NODE_OPP0 + (int)i, "opp-hz", GPUDT_O_ADT, DRV, hz, 2) ||
            !add_u32(t, GPUDT_NODE_OPP0 + (int)i, "opp-microvolt", GPUDT_O_ADT, DRV, uv) ||
            !add_u32(t, GPUDT_NODE_OPP0 + (int)i, "opp-microwatt", GPUDT_O_ADT, DRV, max_pwr[i]))
            return GPUDT_ERR_OVERFLOW;
    }

    /* ---- reserved-memory -------------------------------------------------- */

    {
        const uint32_t two = 2;

        if (!add_u32(t, GPUDT_NODE_RESERVED_MEMORY, "#address-cells", GPUDT_O_STATIC,
                     GPUDT_F_FRAMEWORK, two) ||
            !add_u32(t, GPUDT_NODE_RESERVED_MEMORY, "#size-cells", GPUDT_O_STATIC,
                     GPUDT_F_FRAMEWORK, two) ||
            !add_raw(t, GPUDT_NODE_RESERVED_MEMORY, "ranges", GPUDT_T_EMPTY, GPUDT_O_STATIC,
                     GPUDT_F_FRAMEWORK, NULL, 0))
            return GPUDT_ERR_OVERFLOW;
    }
    /*
     * dt_set_gpu(): gfx-handoff -> uat-handoff, gfx-shared-region ->
     * uat-pagetables, gpu-region -> uat-ttbs. Minimum sizes are the driver's
     * (mmu.rs): one 16K table page, and 64 contexts of two u64 TTBs.
     */
    if ((ret = set_region(t, adt, GPUDT_NODE_UAT_HANDOFF, "gfx-handoff-base", "gfx-handoff-size", 0)) ||
        (ret = set_region(t, adt, GPUDT_NODE_UAT_PAGETABLES, "gfx-shared-region-base",
                          "gfx-shared-region-size", 0x4000)) ||
        (ret = set_region(t, adt, GPUDT_NODE_UAT_TTBS, "gpu-region-base", "gpu-region-size",
                          64 * 16)))
        return ret;
    if ((ret = set_disabled_region(t, GPUDT_NODE_HW_CAL_A)) ||
        (ret = set_disabled_region(t, GPUDT_NODE_HW_CAL_B)) ||
        (ret = set_disabled_region(t, GPUDT_NODE_GLOBALS)))
        return ret;

    /* ---- chosen: what kboot.c derives from the same firmware lookup ------ */

    if (!add_str(t, GPUDT_NODE_CHOSEN, "asahi,iboot1-version", GPUDT_O_FIRMWARE, GPUDT_F_INFO,
                 sys_iboot) ||
        !add_str(t, GPUDT_NODE_CHOSEN, "asahi,system-fw-version", GPUDT_O_FIRMWARE, GPUDT_F_INFO,
                 sys_fw->string) ||
        !add_str(t, GPUDT_NODE_CHOSEN, "asahi,iboot2-version", GPUDT_O_FIRMWARE, GPUDT_F_INFO,
                 os_iboot) ||
        !add_str(t, GPUDT_NODE_CHOSEN, "asahi,os-fw-version", GPUDT_O_FIRMWARE, GPUDT_F_INFO,
                 os_fw->string))
        return GPUDT_ERR_OVERFLOW;

    return GPUDT_OK;
}

const struct gpudt_prop *gpudt_find(const struct gpudt_tree *t, enum gpudt_node_id node,
                                    const char *name)
{
    for (uint32_t i = 0; i < t->nprops; i++)
        if (t->props[i].node == node && !strcmp(t->props[i].name, name))
            return &t->props[i];
    return NULL;
}

char *gpudt_node_path(const struct gpudt_tree *t, enum gpudt_node_id node, char *buf, size_t len)
{
    const struct gpudt_node *n = &t->nodes[node];
    size_t at = 0, l;

    if (!len)
        return buf;
    buf[0] = 0;
    if (n->parent >= 0) {
        gpudt_node_path(t, (enum gpudt_node_id)n->parent, buf, len);
        at = strlen(buf);
        if (at + 1 < len)
            buf[at++] = '/';
    }
    l = strlen(n->name);
    if (l > len - 1 - at)
        l = len - 1 - at;
    memcpy(buf + at, n->name, l);
    buf[at + l] = 0;
    return buf;
}

/* ---- checksums ----------------------------------------------------------- */

uint32_t gpudt_fnv1a(uint32_t h, const void *data, size_t len)
{
    const uint8_t *p = data;

    for (size_t i = 0; i < len; i++)
        h = (h ^ p[i]) * 0x01000193u;
    return h;
}

static bool node_in_subtree(const struct gpudt_tree *t, int node, int subtree)
{
    for (; node >= 0; node = t->nodes[node].parent)
        if (node == subtree)
            return true;
    return subtree < 0;
}

uint32_t gpudt_checksum(const struct gpudt_tree *t, unsigned exclude_flags, int subtree,
                        uint32_t *count)
{
    uint32_t h = GPUDT_FNV1A_INIT, n = 0;
    char path[96];

    for (uint32_t i = 0; i < t->nprops; i++) {
        const struct gpudt_prop *p = &t->props[i];

        if ((p->flags & exclude_flags) || !node_in_subtree(t, p->node, subtree))
            continue;
        gpudt_node_path(t, (enum gpudt_node_id)p->node, path, sizeof(path));
        h = gpudt_fnv1a(h, path, strlen(path) + 1);
        h = gpudt_fnv1a(h, p->name, strlen(p->name) + 1);
        h = gpudt_fnv1a(h, &p->len, 1);
        h = gpudt_fnv1a(h, p->value, p->len);
        n++;
    }
    if (count)
        *count = n;
    return h;
}

/* ---- the IORegistry layout sf_linux_of.c reads --------------------------- */

uint32_t gpudt_prop_to_iodt(const struct gpudt_prop *p, uint8_t *out)
{
    switch (p->type) {
        case GPUDT_T_CELLS:
            for (uint32_t i = 0; i + 4 <= p->len; i += 4)
                wr32le(out + i, rd32be(p->value + i));
            break;
        case GPUDT_T_REG:
            /* each 2-cell field is one native u64: low word first */
            for (uint32_t i = 0; i + 8 <= p->len; i += 8) {
                wr32le(out + i, rd32be(p->value + i + 4));
                wr32le(out + i + 4, rd32be(p->value + i));
            }
            break;
        default:
            if (p->len)
                memcpy(out, p->value, p->len);
            break;
    }
    return p->len;
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);

    return a >= b && !strcmp(s + a - b, suffix);
}

/* sf_linux_of.c looks_like_strings() */
static bool looks_like_strings(const uint8_t *v, uint32_t len)
{
    uint32_t start = 0;

    if (len == 0 || v[len - 1] != 0)
        return false;
    for (uint32_t i = 0; i < len; i++) {
        if (v[i] == 0) {
            if (i == start)
                return false;
            start = i + 1;
        } else if (v[i] < 0x20 || v[i] > 0x7e) {
            return false;
        }
    }
    return true;
}

/*
 * sf_linux_of.c to_fdt() for a node whose parent has #address-cells = 2 and
 * #size-cells = 2 (which is how every node of this tree with a "reg" is to be
 * attached). "ranges" only occurs empty here.
 */
static void model_to_fdt(const char *name, const uint8_t *in, uint32_t len, uint8_t *out)
{
    bool numeric = !strcmp(name, "reg") || !strcmp(name, "ranges") || !strcmp(name, "dma-ranges") ||
                   !strcmp(name, "phandle") || !strcmp(name, "linux,phandle") ||
                   !strcmp(name, "interrupt-parent") || !strcmp(name, "interrupts") ||
                   !strcmp(name, "interrupts-extended") || name[0] == '#';

    if ((!numeric && (looks_like_strings(in, len) || ends_with(name, "-names"))) || len % 4 != 0) {
        memcpy(out, in, len);
        return;
    }
    if (!strcmp(name, "reg") && len % 16 == 0) {
        for (uint32_t i = 0; i + 8 <= len; i += 8) {
            wr32be(out + i, rd32le(in + i + 4));
            wr32be(out + i + 4, rd32le(in + i));
        }
        return;
    }
    if (len == 8 && (ends_with(name, "-frequency") || !strncmp(name, "linux,initrd-", 13))) {
        wr32be(out, rd32le(in + 4));
        wr32be(out + 4, rd32le(in));
        return;
    }
    for (uint32_t i = 0; i + 4 <= len; i += 4)
        wr32be(out + i, rd32le(in + i));
}

bool gpudt_prop_iodt_roundtrips(const struct gpudt_prop *p)
{
    uint8_t iodt[GPUDT_MAX_VALUE], back[GPUDT_MAX_VALUE];

    if (!p->len)
        return true;
    gpudt_prop_to_iodt(p, iodt);
    model_to_fdt(p->name, iodt, p->len, back);
    return !memcmp(back, p->value, p->len);
}
