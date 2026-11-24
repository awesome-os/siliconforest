/* SPDX-License-Identifier: MIT */
/*
 * m1-gpu-dt: Linux-style device-tree properties for the Apple T8103 (M1, G13G)
 * GPU, synthesised from Apple's device tree (ADT).
 *
 * The T8103 computations (operating points, power table, leakage
 * coefficients, UAT regions, firmware version tuples) are ported from m1n1
 * src/kboot_gpu.c, src/kboot.c and src/firmware.c, MIT, Copyright The Asahi
 * Linux Contributors. The static values are those of the Linux device tree
 * source arch/arm64/boot/dts/apple/t8103.dtsi (GPL-2.0+ OR MIT).
 *
 * Portable C11. No kernel headers, no floating point, no allocation, no I/O:
 * everything goes through the accessor callback and the caller's storage.
 */
#ifndef M1_GPU_DT_GPUDT_H
#define M1_GPU_DT_GPUDT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GPUDT_MAX_NODES 20
#define GPUDT_MAX_PROPS 96
#define GPUDT_MAX_VALUE 64
#define GPUDT_MAX_DIAGS 48

#define GPUDT_CHIP_T8103 0x8103u

/* Number of operating points of the T8103 GPU (static DTS, and m1n1's t8103_pwr_scale[]). */
#define GPUDT_T8103_NUM_OPPS 7
/* Core slots of G13G; bit n of the SGX core-mask register is core n. */
#define GPUDT_T8103_MAX_CORES 8
/* SGX register holding the present-core mask, relative to the "sgx" reg window (m1n1 get_core_counts). */
#define GPUDT_T8103_SGX_CORE_MASK_REG 0xd01500u

/*
 * Reads one ADT property. node_path is an absolute ADT path ("/arm-io/sgx").
 * Returns the raw bytes exactly as the ADT stores them (little-endian) and
 * their length, or NULL when the node or the property does not exist.
 */
struct gpudt_adt_ops {
    void *ctx;
    const void *(*getprop)(void *ctx, const char *node_path, const char *name, uint32_t *len);
};

struct gpudt_config {
    uint32_t chip_id; /* must be GPUDT_CHIP_T8103 */

    /*
     * Present-core mask, read by the caller from SGX + GPUDT_T8103_SGX_CORE_MASK_REG
     * with the GPU power domain on. This is the one input that is not in the
     * ADT. Without it (have_core_mask false) the build fails, unless
     * assume_all_cores is set, which uses the full 8-core mask and records a
     * warning.
     */
    bool have_core_mask;
    uint32_t core_mask;
    bool assume_all_cores;

    /*
     * phandle numbers. The library assigns phandle_base + 0..6 to the nodes it
     * creates (ttbs, pagetables, handoff, hw-cal-a, hw-cal-b, globals,
     * opp-table). mboxes and power-domains name nodes this library does not
     * create; with a phandle of 0 the property is left out.
     */
    uint32_t phandle_base;
    uint32_t mbox_phandle;
    uint32_t power_domain_phandle;
};

enum gpudt_node_id {
    GPUDT_NODE_GPU = 0,         /* gpu@206400000 */
    GPUDT_NODE_RESERVED_MEMORY, /* reserved-memory */
    GPUDT_NODE_GLOBALS,         /* reserved-memory/globals (disabled) */
    GPUDT_NODE_HW_CAL_A,        /* reserved-memory/hw-cal-a (disabled) */
    GPUDT_NODE_HW_CAL_B,        /* reserved-memory/hw-cal-b (disabled) */
    GPUDT_NODE_UAT_HANDOFF,     /* reserved-memory/uat-handoff@10 */
    GPUDT_NODE_UAT_PAGETABLES,  /* reserved-memory/uat-pagetables@20 */
    GPUDT_NODE_UAT_TTBS,        /* reserved-memory/uat-ttbs@30 */
    GPUDT_NODE_OPP_TABLE,       /* opp-table-gpu */
    GPUDT_NODE_OPP0,            /* opp-table-gpu/opp00 .. opp06 */
    GPUDT_NODE_CHOSEN = GPUDT_NODE_OPP0 + GPUDT_T8103_NUM_OPPS, /* chosen (informational) */
    GPUDT_NODE_COUNT
};

/* How the value bytes are laid out. Values are always stored as a Linux FDT has them. */
enum gpudt_type {
    GPUDT_T_EMPTY = 0, /* zero-length flag property */
    GPUDT_T_STRINGS,   /* one or more NUL-terminated strings */
    GPUDT_T_CELLS,     /* big-endian u32 cells (also f32 bit patterns and 2-cell u64) */
    GPUDT_T_REG,       /* big-endian (address, size) tuples of 2 + 2 cells */
};

/* Where Linux gets the value. */
enum gpudt_origin {
    GPUDT_O_STATIC = 0, /* Linux device tree source */
    GPUDT_O_ADT,        /* m1n1 computes it from the ADT */
    GPUDT_O_ADT_HW,     /* m1n1 computes it from the ADT and a hardware register */
    GPUDT_O_FIRMWARE,   /* m1n1's firmware table, keyed by the ADT /chosen iBoot string */
    GPUDT_O_SYNTH,      /* numbering or tree layout chosen by whoever builds the tree */
};

/* Cross-check of a static value against the ADT property carrying the same quantity. */
enum gpudt_xcheck {
    GPUDT_X_NONE = 0, /* no ADT counterpart */
    GPUDT_X_MATCH,
    GPUDT_X_MISMATCH, /* the static value is still the one emitted */
    GPUDT_X_ABSENT,   /* the expected ADT property is missing */
};

#define GPUDT_F_DRIVER_READS 0x01u /* the T8103/G13 driver path reads it */
#define GPUDT_F_PHANDLE      0x02u /* cells are phandle numbers */
#define GPUDT_F_INFO         0x04u /* not needed by the driver; emitted for comparison only */
#define GPUDT_F_FRAMEWORK    0x08u /* read by the Linux core (rtkit/mailbox, genpd, platform), not the driver */
#define GPUDT_F_RUNTIME      0x10u /* stands in for something m1n1 allocates at boot; differs from Linux by design */

/* phandle_base the Darwin kext uses; no entry of Apple's device tree has a "phandle" property */
#define GPUDT_DARWIN_PHANDLE_BASE 0x53460000u

struct gpudt_prop {
    uint8_t node;   /* enum gpudt_node_id */
    uint8_t type;   /* enum gpudt_type */
    uint8_t origin; /* enum gpudt_origin */
    uint8_t xcheck; /* enum gpudt_xcheck */
    uint8_t flags;
    uint8_t len;
    const char *name; /* static storage */
    uint8_t value[GPUDT_MAX_VALUE];
};

struct gpudt_node {
    const char *name; /* static storage */
    int8_t parent;    /* enum gpudt_node_id, or -1 for a top-level node */
    bool present;
    uint32_t phandle; /* 0 when the node has none */
};

enum gpudt_diag_code {
    GPUDT_D_ASSUMED_ALL_CORES = 1,  /* a = mask used */
    GPUDT_D_FIRMWARE_UNKNOWN,       /* iBoot string not in the table; tuples are 0 0 0 */
    GPUDT_D_FIRMWARE_UNSUPPORTED,   /* a,b = compat major, minor; the driver has no G13 ABI for it */
    GPUDT_D_STATIC_ADT_MISMATCH,    /* text = property; a = static, b = ADT */
    GPUDT_D_STATIC_ADT_ABSENT,      /* text = property */
    GPUDT_D_REG_ADT_MISMATCH,       /* text = reg name; a,b = static, ADT (low 32 bits) */
    GPUDT_D_NO_MBOX_PHANDLE,
    GPUDT_D_NO_POWER_DOMAIN_PHANDLE,
    GPUDT_D_REDUCED_CORES,          /* a = core count, fewer than 8 (informational) */
    GPUDT_D_REGION_TOO_SMALL,       /* text = region; a = size, b = required */
    GPUDT_D_SYSTEM_FIRMWARE_ABSENT,
    GPUDT_D_VALUE_TOO_LONG,         /* text = property */
};

struct gpudt_diag {
    uint16_t code;
    const char *text; /* static storage or NULL */
    uint64_t a, b;
};

struct gpudt_tree {
    struct gpudt_node nodes[GPUDT_NODE_COUNT];
    struct gpudt_prop props[GPUDT_MAX_PROPS];
    uint32_t nprops;
    struct gpudt_diag diags[GPUDT_MAX_DIAGS];
    uint32_t ndiags;

    /* Decoded inputs, for the caller's log. */
    uint32_t core_mask;
    bool core_mask_assumed;
    uint32_t core_count;
    uint32_t fw_version[4], fw_version_len;
    uint32_t fw_compat[4], fw_compat_len;
    const char *fw_string;
};

/* Errors: the ADT conditions under which m1n1 refuses, plus this library's own. */
#define GPUDT_OK                    0
#define GPUDT_ERR_ARGS              (-1)
#define GPUDT_ERR_CHIP              (-2)
#define GPUDT_ERR_NO_SGX            (-3)  /* /arm-io/sgx or perf-state-count missing */
#define GPUDT_ERR_PERF_STATE_COUNT  (-4)  /* not 7, or table count not 1 */
#define GPUDT_ERR_PERF_STATES       (-5)  /* perf-states missing or of the wrong length */
#define GPUDT_ERR_REGION            (-6)  /* a <name>-base/-size property missing or zero */
#define GPUDT_ERR_NO_FIRMWARE       (-7)  /* /chosen firmware-version missing or unterminated */
#define GPUDT_ERR_NEED_CORE_MASK    (-8)
#define GPUDT_ERR_OVERFLOW          (-9)  /* out of property slots */

/*
 * Builds the tree. *out is fully overwritten. On a negative return the tree
 * is incomplete and must not be published.
 */
int gpudt_build_t8103(const struct gpudt_adt_ops *adt, const struct gpudt_config *cfg,
                      struct gpudt_tree *out);

const struct gpudt_prop *gpudt_find(const struct gpudt_tree *t, enum gpudt_node_id node,
                                    const char *name);

/* Writes "a/b/c" for the node into buf (always NUL-terminated); returns buf. */
char *gpudt_node_path(const struct gpudt_tree *t, enum gpudt_node_id node, char *buf, size_t len);

/*
 * The value as SiliconForest's sf_linux_of.c expects to find it on an IORegistry
 * entry of the device-tree plane (the layout XNU's own device tree has):
 * strings unchanged, u32 cells little-endian, the 2-cell fields of "reg" as
 * one little-endian u64 each. out must hold p->len bytes. Returns p->len.
 */
uint32_t gpudt_prop_to_iodt(const struct gpudt_prop *p, uint8_t *out);

/*
 * Whether sf_linux_of.c's to_fdt() gives back exactly p->value for the bytes
 * gpudt_prop_to_iodt() produces. False means that layer would hand the driver
 * a wrong value (a numeric value whose bytes pass for text, or a string
 * property under a name it treats as numeric).
 */
bool gpudt_prop_iodt_roundtrips(const struct gpudt_prop *p);

/*
 * FNV-1a (32 bit) over the properties whose flags have none of exclude_flags
 * set and whose node is `subtree` or below it (subtree < 0: every node), in
 * table order. Per property: node path, NUL, name, NUL, one length byte, the
 * value in FDT byte order. *count, if not NULL, gets the number hashed.
 *
 * With GPUDT_CHECKSUM_LINUX the result depends only on values Linux has too:
 * it leaves out phandle numbers and the stand-ins for m1n1's boot-time blobs.
 */
#define GPUDT_CHECKSUM_ALL   0u
#define GPUDT_CHECKSUM_LINUX (GPUDT_F_PHANDLE | GPUDT_F_RUNTIME)
uint32_t gpudt_checksum(const struct gpudt_tree *t, unsigned exclude_flags, int subtree,
                        uint32_t *count);
#define GPUDT_FNV1A_INIT 0x811c9dc5u
uint32_t gpudt_fnv1a(uint32_t h, const void *data, size_t len);

/*
 * The summary the kext logs and the host test predicts (gpudt_summary.c, the
 * only part that needs snprintf). emit() gets one line at a time, without a
 * newline.
 */
void gpudt_summary(const struct gpudt_tree *t, void (*emit)(void *ctx, const char *line), void *ctx);

/* IEEE-754 single precision on bit patterns, round to nearest even, finite non-negative values only. */
uint32_t gpudt_f32_from_u32(uint32_t v);
uint32_t gpudt_f32_add(uint32_t a, uint32_t b);
uint32_t gpudt_f32_sub(uint32_t a, uint32_t b); /* a >= b */
uint32_t gpudt_f32_mul(uint32_t a, uint32_t b);
uint32_t gpudt_f32_div(uint32_t a, uint32_t b);

/* m1n1 adjust_leakage() for one cluster, on bit patterns. */
uint32_t gpudt_adjust_leakage(uint32_t val, uint32_t cores, uint32_t max_cores,
                              uint32_t uncore_fraction);

#endif
