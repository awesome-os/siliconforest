#define _DARWIN_C_SOURCE

/*
 * Observation-only page-size and vm_protect probe.
 * One JSON object on stdout. No success verdict.
 * Does not read the anonymous mapping, including after PROT_NONE.
 * Does not change malloc guard policy and does not dereference the commpage.
 *
 * Every vm_protect goes through checked_vm_protect. That helper refuses a
 * span outside the one anonymous mapping, then requires the address and
 * length to survive the header typedefs:
 *   (uintptr_t)(vm_address_t)address == address
 *   (size_t)(vm_size_t)length == length
 * The typedef width is whatever mach/mach.h selected. This file does not
 * assume it. A failed round trip is recorded and the Mach call is skipped.
 * mprotect still uses the original void * and size_t when that span is
 * inside the mapping.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <mach/mach.h>
#include <sys/mman.h>

enum {
	k_map_bytes = 65536,
	k_span_bytes = 16384,
	k_align = 16384,
	k_kind_vm_none = 1,
	k_kind_mp_none = 2,
	k_kind_vm_restore = 3,
	k_kind_mp_restore = 4
};

static const unsigned k_offsets[3] = {0u, 4096u, 8192u};

static void
emit_sep(int *count)
{
	if (*count > 0) {
		fputc(',', stdout);
	}
	*count += 1;
}

static void
emit_called(int *count, int kind, int whole, unsigned offset, int length, int ret, int err)
{
	emit_sep(count);
	printf("{\"kind\":%d,\"whole\":%d,\"offset\":%u,\"length\":%d,"
	    "\"called\":1,\"abi_truncation\":0,\"outside_region\":0,"
	    "\"ret\":%d,\"errno\":%d}",
	    kind, whole, offset, length, ret, err);
}

/* No ret and no errno: a skipped call must not look like KERN_SUCCESS. */
static void
emit_skipped(int *count, int kind, int whole, unsigned offset, int length,
    int abi_truncation, int outside_region)
{
	emit_sep(count);
	printf("{\"kind\":%d,\"whole\":%d,\"offset\":%u,\"length\":%d,"
	    "\"called\":0,\"abi_truncation\":%d,\"outside_region\":%d}",
	    kind, whole, offset, length, abi_truncation, outside_region);
}

static int
span_inside(uintptr_t region, size_t region_len, uintptr_t address, size_t length)
{
	uintptr_t distance;

	if (length == 0 || length > region_len) {
		return 0;
	}
	if (address < region) {
		return 0;
	}
	distance = address - region;
	if (distance > (uintptr_t)(region_len - length)) {
		return 0;
	}
	return 1;
}

/*
 * set_maximum stays false so the original maximum protection remains
 * read/write. The only vm_protect in this program is the one below.
 */
static void
checked_vm_protect(int *count, int kind, int whole, unsigned offset,
    void *region, size_t region_len, uintptr_t address, int length, vm_prot_t prot)
{
	size_t nbytes;
	vm_address_t mach_addr;
	vm_size_t mach_len;
	kern_return_t kr;
	int err;

	if (length <= 0) {
		emit_skipped(count, kind, whole, offset, length, 0, 1);
		return;
	}
	nbytes = (size_t)length;
	if (!span_inside((uintptr_t)region, region_len, address, nbytes)) {
		emit_skipped(count, kind, whole, offset, length, 0, 1);
		return;
	}
	mach_addr = (vm_address_t)address;
	mach_len = (vm_size_t)nbytes;
	if ((uintptr_t)mach_addr != address || (size_t)mach_len != nbytes) {
		emit_skipped(count, kind, whole, offset, length, 1, 0);
		return;
	}
	errno = 0;
	kr = vm_protect(mach_task_self(), mach_addr, mach_len, FALSE, prot);
	err = errno;
	emit_called(count, kind, whole, offset, length, (int)kr, err);
}

static void
observe_mprotect(int *count, int kind, int whole, unsigned offset,
    void *region, size_t region_len, uintptr_t address, int length, int prot)
{
	size_t nbytes;
	int rc;
	int err;

	if (length <= 0) {
		emit_skipped(count, kind, whole, offset, length, 0, 1);
		return;
	}
	nbytes = (size_t)length;
	if (!span_inside((uintptr_t)region, region_len, address, nbytes)) {
		emit_skipped(count, kind, whole, offset, length, 0, 1);
		return;
	}
	errno = 0;
	rc = mprotect((void *)address, nbytes, prot);
	err = errno;
	emit_called(count, kind, whole, offset, length, rc, err);
}

static void
restore_whole(void *map, int *count)
{
	checked_vm_protect(count, k_kind_vm_restore, 1, 0u, map,
	    (size_t)k_map_bytes, (uintptr_t)map, k_map_bytes,
	    VM_PROT_READ | VM_PROT_WRITE);
	observe_mprotect(count, k_kind_mp_restore, 1, 0u, map,
	    (size_t)k_map_bytes, (uintptr_t)map, k_map_bytes,
	    PROT_READ | PROT_WRITE);
}

int
main(void)
{
	int pagesize;
	long sc_pagesize;
	int sc_errno;
	vm_size_t host_pages;
	kern_return_t host_kr;
	int host_errno;
	void *map;
	int map_errno;
	int map_ok;
	uintptr_t raw;
	uintptr_t base;
	size_t slack;
	int base_ok;
	int probe_count;
	unsigned n;
	int munmap_errno;

	pagesize = getpagesize();

	errno = 0;
	sc_pagesize = sysconf(_SC_PAGESIZE);
	sc_errno = errno;

	host_pages = 0;
	errno = 0;
	host_kr = host_page_size(mach_host_self(), &host_pages);
	host_errno = errno;

	errno = 0;
	map = mmap(NULL, (size_t)k_map_bytes, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANON, -1, 0);
	map_errno = errno;
	map_ok = (map != MAP_FAILED);

	raw = 0;
	base = 0;
	slack = 0;
	base_ok = 0;
	if (map_ok) {
		raw = (uintptr_t)map;
		if (raw <= UINTPTR_MAX - (uintptr_t)(k_align - 1)) {
			uintptr_t aligned;

			aligned = (raw + (uintptr_t)(k_align - 1)) &
			    ~((uintptr_t)k_align - 1);
			if (aligned >= raw &&
			    (aligned - raw) < (uintptr_t)k_map_bytes) {
				base = aligned;
				slack = (size_t)(aligned - raw);
				base_ok = 1;
			}
		}
	}

	printf("{\"record\":\"OBSERVATION_ONLY\"");
	printf(",\"commpage_read\":0");
	printf(",\"sizeof_vm_address_t\":%llu",
	    (unsigned long long)sizeof(vm_address_t));
	printf(",\"sizeof_vm_size_t\":%llu",
	    (unsigned long long)sizeof(vm_size_t));
	printf(",\"sizeof_void_star\":%llu",
	    (unsigned long long)sizeof(void *));
	printf(",\"getpagesize\":%d", pagesize);
	printf(",\"sysconf_sc_pagesize\":%ld,\"sysconf_errno\":%d",
	    sc_pagesize, sc_errno);
	printf(",\"host_page_size_ret\":%d,\"host_page_size_errno\":%d,\"host_page_size\":%llu",
	    (int)host_kr, host_errno, (unsigned long long)host_pages);
	printf(",\"vm_page_size\":%llu,\"vm_page_mask\":%llu,\"vm_page_shift\":%d",
	    (unsigned long long)vm_page_size,
	    (unsigned long long)vm_page_mask,
	    vm_page_shift);
	printf(",\"vm_kernel_page_size\":%llu,\"vm_kernel_page_mask\":%llu,\"vm_kernel_page_shift\":%d",
	    (unsigned long long)vm_kernel_page_size,
	    (unsigned long long)vm_kernel_page_mask,
	    vm_kernel_page_shift);
	printf(",\"map_bytes\":%d,\"span_bytes\":%d", k_map_bytes, k_span_bytes);
	printf(",\"mmap_ok\":%d,\"mmap_errno\":%d", map_ok, map_ok ? 0 : map_errno);
	if (map_ok) {
		printf(",\"base_inside\":%d", base_ok);
		if (base_ok) {
			printf(",\"base_offset\":%llu,\"base_mod_4096\":%llu,\"base_mod_16384\":%llu",
			    (unsigned long long)slack,
			    (unsigned long long)(base % 4096u),
			    (unsigned long long)(base % 16384u));
		}
	}

	probe_count = 0;
	printf(",\"probes\":[");
	if (map_ok && base_ok) {
		for (n = 0; n < 3; n++) {
			unsigned off;
			uintptr_t span;

			off = k_offsets[n];
			if ((uintptr_t)off > UINTPTR_MAX - base) {
				emit_skipped(&probe_count, k_kind_vm_none, 0, off,
				    k_span_bytes, 0, 1);
				restore_whole(map, &probe_count);
				emit_skipped(&probe_count, k_kind_mp_none, 0, off,
				    k_span_bytes, 0, 1);
				restore_whole(map, &probe_count);
				continue;
			}
			span = base + (uintptr_t)off;
			checked_vm_protect(&probe_count, k_kind_vm_none, 0, off, map,
			    (size_t)k_map_bytes, span, k_span_bytes, VM_PROT_NONE);
			restore_whole(map, &probe_count);
			observe_mprotect(&probe_count, k_kind_mp_none, 0, off, map,
			    (size_t)k_map_bytes, span, k_span_bytes, PROT_NONE);
			restore_whole(map, &probe_count);
		}
	}
	if (map_ok) {
		restore_whole(map, &probe_count);
	}
	printf("]");

	munmap_errno = 0;
	if (map_ok) {
		errno = 0;
		if (munmap(map, (size_t)k_map_bytes) != 0) {
			munmap_errno = errno;
		}
		printf(",\"munmap_errno\":%d", munmap_errno);
	}
	printf("}\n");
	fflush(stdout);
	return 0;
}
