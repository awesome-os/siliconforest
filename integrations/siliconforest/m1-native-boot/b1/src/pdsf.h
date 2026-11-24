/* SPDX-License-Identifier: MIT */
/*
 * SiliconForest stage: what m1n1 needs to boot the SiliconForest XNU by itself.
 *
 * This header and pdsf.c are the part with no hardware in it: the payload container, the
 * configuration text, the Mach-O fileset loader, the memory layout, and the rewrite of the
 * Apple device tree. They compile for the host as well (-DPDSF_HOST), where test_b1.py checks
 * them against Python models. pdsf_boot.c is the m1n1 side.
 */
#ifndef PDSF_H
#define PDSF_H

#ifdef PDSF_HOST
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include "string.h"
#include "types.h"
#include "utils.h"
#endif

/*
 * Container: records after the m1n1 image, where m1n1 looks for its payloads. Each record is
 * a 32-byte header and `stored` bytes; the next record starts at the next multiple of 16. The
 * list ends with a record of type PDSF_END. A gzip record is inflated by the stage; `raw` is
 * the size after that (equal to `stored` otherwise). crc32 covers the stored bytes.
 */
#define PDSF_MAGIC   0x46534450u /* "PDSF" */
#define PDSF_VERSION 1
#define PDSF_ALIGN   16

enum { PDSF_END = 0, PDSF_CONFIG = 1, PDSF_KERNEL = 2, PDSF_RAMDISK = 3, PDSF_TYPES };
#define PDSF_F_GZIP 1u

struct pdsf_record {
    u32 magic;
    u16 version;
    u16 type;
    u32 flags;
    u32 crc32;
    u64 stored;
    u64 raw;
};

struct pdsf_item {
    const u8 *data;
    u64 stored, raw;
    u32 flags, crc32;
    int present;
};

struct pdsf_set {
    struct pdsf_item item[PDSF_TYPES];
    const u8 *end; /* first byte after the END record */
};

/* 1: no container here (not ours); 0: complete list; < 0: a damaged list */
int pdsf_scan(const u8 *p, u64 limit, struct pdsf_set *set);

/* Configuration: lines of key=value */
#define PDSF_CMDLINE_MAX 1024
#define PDSF_SEED_BYTES  256
enum { PDSF_USB_KEEP = -1 };

struct pdsf_config {
    char bootargs[PDSF_CMDLINE_MAX];
    u32 display;    /* boot_args video.display; default 1 (graphics, console on the UART) */
    int usb_remove; /* PDSF_USB_KEEP, or the Type-C port whose nodes are removed */
    u32 sprr, amx;  /* as the EL2 page's flags; default 1, 1 */
    u32 wdt;        /* seconds, 0: do not arm the SoC watchdog */
    u32 reserve_mb; /* memory kept for m1n1 between its heap and XNU's first byte */
    u32 serial;     /* 1: /defaults/serial-device = uart0 */
    u64 xnu_base;   /* 0: the stage chooses the first byte of XNU's memory; else that address */
    int have_seed;  /* bench only: seed given in the configuration instead of the SEP */
    u8 seed[PDSF_SEED_BYTES];
};

int pdsf_config_parse(const char *text, u64 len, struct pdsf_config *cfg, const char **err);
/* N of the last cpus=N token of a command line, 0 when there is none */
int pdsf_cmdline_cpus(const char *cmdline);
int pdsf_cmdline_has(const char *cmdline, const char *token);
/* May this stage boot the root the command line names? 0 and kind 1 (RAM root) or 2 (the disk
 * root it was built for), or -1 and a reason. See pdsf.c. */
int pdsf_root_check(const char *cmdline, int have_ramdisk, const char *built_for, int *kind, const char **err);

/* Mach-O (MH_FILESET kernel collection) */
struct pdsf_macho {
    u64 vmin, vmax, entry;
    u32 segments;
};

int pdsf_macho_info(const u8 *kc, u64 size, struct pdsf_macho *m, const char **err);
/* Writes vmax - vmin bytes at dest: zero, then every segment's file bytes at vmaddr - vmin */
int pdsf_macho_load(const u8 *kc, u64 size, const struct pdsf_macho *m, u8 *dest);

/*
 * Memory layout, as the launcher's (m1n1 hv load_raw + host_progress.load_payloads):
 *   phys_base                  first byte of RAM given to XNU: the next 32 MiB boundary above
 *                              m1n1's memory and its reserve, or the address asked for
 *   + 16 MiB + ADT + TC        left unused (stock load_raw's place for ADT and TrustCache)
 *   guest_base                 kernel image (vmax - vmin)
 *   sepfw, preoslog, bootargs  after the image, each on a 16 KiB boundary
 *   adt, trustcache, ramdisk   after the boot arguments
 *   top                        top_of_kernel_data
 */
#define PDSF_PAGE 0x4000ull

struct pdsf_layout_in {
    u64 stage_top;  /* first byte m1n1 does not use */
    u64 reserve;    /* bytes kept for m1n1 above stage_top */
    u64 fixed_base; /* 0, or the phys_base asked for (not below stage_top + reserve) */
    u64 ram_end;    /* phys_base + mem_size of the boot arguments m1n1 got */
    u64 image_size; /* vmax - vmin */
    u64 devtree_size, adt_capacity, sepfw_size, preoslog_size, tc_size, ramdisk_size;
};

struct pdsf_layout {
    u64 phys_base, virt_base, mem_size, guest_base;
    u64 sepfw, preoslog, bootargs, adt, tc, ramdisk, top;
};

int pdsf_layout(const struct pdsf_layout_in *in, struct pdsf_layout *out);

/*
 * Device tree. The tree is copied node by node into a new buffer; on the way
 *   - nodes named in `remove` (full paths, "/arm-io/dart-usb0") are left out,
 *   - with prune_cpus, children of /cpus whose state is not "running" are left out,
 *   - properties in `set` replace the node's property of that name, or are added to it
 *     (unless only_existing),
 *   - with strip_oslog, "__OS_LOG" leaves segment-names and segment-ranges of the children
 *     and grandchildren of /arm-io,
 *   - with dfp_remove, that name leaves dfp-endpoints of /arm-io/display-crossbar* nodes.
 */
struct pdsf_adt_set {
    const char *node, *prop;
    const void *value;
    u32 len;
    int only_existing;
    int used; /* out: how many nodes took it */
};

struct pdsf_adt_rules {
    struct pdsf_adt_set *set;
    u32 nset;
    const char *const *remove;
    u32 nremove;
    int prune_cpus, strip_oslog;
    const char *dfp_remove;
    u32 removed, oslog_stripped, dfp_edited; /* out */
};

/* Bytes written, or < 0 */
long pdsf_adt_rebuild(const u8 *src, u64 srclen, u8 *dst, u64 cap, struct pdsf_adt_rules *rules);
/* Bytes one tree occupies, or < 0 when it does not parse within srclen */
long pdsf_adt_size(const u8 *src, u64 srclen);

#ifndef PDSF_HOST
/* pdsf_boot.c. 1: no SiliconForest payload at `payload`; 0: next_stage is set; < 0: failed */
int pdsf_boot(void *payload);
/* The same for a record list anywhere in RAM (`appended` 0: in m1n1's heap) */
int pdsf_boot_records(const u8 *payload, u64 limit, int appended);
/* Installed stage 2 only (../install/src/pdsf_file.c, SF_KERNEL_FILE): the same from the file
 * PDSF_KERNEL_FILE of the FAT partition with this PARTUUID. 0: next_stage is set; < 0: failed */
#define PDSF_KERNEL_FILE "sf/kernel.bin"
int pdsf_boot_file(const char *partuuid);
#endif

#endif
