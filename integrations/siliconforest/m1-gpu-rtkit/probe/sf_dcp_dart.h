/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The display coprocessor's DART (/arm-io/dart-dcp, T8020, stream 0) as SFRtkitProbe's DCP
 * mode needs it for SF-H item H2: translate an address the firmware placed, and map a buffer
 * the firmware asked the AP for. See sf_dcp_dart.cpp for what is and is not written.
 * /arm-io/dart-disp0 is never opened, read or written by this code.
 */
#ifndef SF_DCP_DART_H
#define SF_DCP_DART_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read-only. Finds the DART in the device tree, maps its registers, checks that stream 0
 * is the translating, firmware-provisioned stream this code understands, maps the live
 * first-level table and picks the first-level slot later allocations will use. Logs
 * `dcp dart-regs`, `dcp dart-l1`, `dcp dart-window`. 0, or a negative errno after a
 * `dcp dart-stop` line saying which check failed.
 */
int sf_dcp_dart_open(void);

/*
 * Read-only walk for an address as the firmware writes it (0xf00267000): logs one
 * `dcp dart-walk` line with both table entries and the result. Returns the physical
 * address of that page, or 0 when it is not mapped.
 */
uint64_t sf_dcp_dart_walk(const char *what, uint64_t fw_addr);

/*
 * Read-only check of the walk against something known: the firmware's own segments
 * (iop-dcp-nub segment-ranges: physical address and the address the firmware runs them
 * at) must translate to the physical addresses the device tree gives, as m1n1
 * dcp_create_firmware_mappings() expects. Logs one `dcp dart-selfcheck` line per segment.
 * Returns the number of segments that translate correctly, 0 if none or none described.
 */
int sf_dcp_dart_selfcheck(void);

/*
 * A buffer the firmware placed: translates every page (read-only), requires them to be
 * physically contiguous, and maps the physical range into the kernel. Logs
 * `dcp dart-fwbuf`. Returns the kernel address of fw_addr, or NULL.
 */
void *sf_dcp_dart_map_fw(uint64_t fw_addr, size_t size);
void sf_dcp_dart_unmap_fw(void *kva);

/*
 * A buffer the firmware asked the AP for: allocates wired, zeroed, contiguous memory, maps
 * it in the DART, invalidates the stream's TLB, reads the entries back and walks the new
 * address. Logs `dcp dart-l2` (first use only), `dcp dart-map`, `dcp dart-tlb`,
 * `dcp dart-verify`. Returns the kernel address and the address to hand the firmware, or
 * NULL with nothing left mapped.
 */
void *sf_dcp_dart_alloc(size_t size, uint64_t *fw_addr);
/* Clears the entries of an allocation and invalidates. The memory is kept until reboot. */
void sf_dcp_dart_release(uint64_t fw_addr, size_t size);

/* Reads the DART's error register (never clears it); logs `dcp dart-error` if a fault is flagged. */
uint32_t sf_dcp_dart_check(const char *when);

#ifdef __cplusplus
}
#endif

#endif
