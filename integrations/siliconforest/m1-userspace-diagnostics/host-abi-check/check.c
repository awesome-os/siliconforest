/* Host-only fixture. Includes the frozen page-probe.c. Does not run main. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <mach/mach.h>

int fake_vm_protect_calls;
static int fake_set_maximum;
static kern_return_t fake_ret = 42;

vm_size_t vm_page_size;
vm_size_t vm_page_mask;
int vm_page_shift;
vm_size_t vm_kernel_page_size;
vm_size_t vm_kernel_page_mask;
int vm_kernel_page_shift;

kern_return_t
vm_protect(vm_map_t task, vm_address_t address, vm_size_t length,
    boolean_t set_maximum, vm_prot_t prot)
{
	(void)task;
	(void)address;
	(void)length;
	(void)prot;
	fake_vm_protect_calls += 1;
	fake_set_maximum = (int)set_maximum;
	return fake_ret;
}

kern_return_t
host_page_size(vm_map_t host, vm_size_t *out)
{
	(void)host;
	if (out != NULL) {
		*out = 0;
	}
	return 0;
}

vm_map_t
mach_host_self(void)
{
	return 0;
}

vm_map_t
mach_task_self(void)
{
	return 0;
}

#define main page_probe_main
#include "../page-probe.c"
#undef main

static int fails;
static char cap[512];

static void
begin_capture(int *saved, int *read_fd)
{
	int fds[2];

	fflush(stdout);
	if (pipe(fds) != 0) {
		perror("pipe");
		fails += 1;
		*saved = -1;
		*read_fd = -1;
		return;
	}
	*saved = dup(1);
	dup2(fds[1], 1);
	close(fds[1]);
	*read_fd = fds[0];
}

static void
end_capture(int saved, int read_fd)
{
	ssize_t n;

	fflush(stdout);
	if (saved >= 0) {
		dup2(saved, 1);
		close(saved);
	}
	n = 0;
	if (read_fd >= 0) {
		n = read(read_fd, cap, sizeof cap - 1);
		close(read_fd);
	}
	if (n < 0) {
		n = 0;
	}
	cap[n] = 0;
}

static void
expect(const char *name, int before, int want_calls, const char *snippet)
{
	int delta;
	int ok;

	delta = fake_vm_protect_calls - before;
	ok = (delta == want_calls);
	if (snippet != NULL && strstr(cap, snippet) == NULL) {
		ok = 0;
	}
	printf("%s %s\n", name, ok ? "PASS" : "FAIL");
	if (!ok) {
		fails += 1;
	}
}

static uintptr_t
low_base(void)
{
	return (uintptr_t)0x10000u;
}

static uintptr_t
high_base(void)
{
	return ((uintptr_t)1 << 32) + (uintptr_t)0x10000u;
}

static void
check_ranges(void)
{
	uintptr_t base;
	int before;
	int count;
	int saved;
	int read_fd;

	base = low_base();
	if (span_inside(base, 65536u, base - 1u, 4096u) != 0) {
		printf("span_below FAIL\n");
		fails += 1;
	} else {
		printf("span_below PASS\n");
	}
	if (span_inside(base, 65536u, base + 65536u - 4096u + 1u, 4096u) != 0) {
		printf("span_past_end FAIL\n");
		fails += 1;
	} else {
		printf("span_past_end PASS\n");
	}
	if (span_inside(base, 65536u, base, 0u) != 0) {
		printf("span_zero_length FAIL\n");
		fails += 1;
	} else {
		printf("span_zero_length PASS\n");
	}
	if (span_inside(base, 65536u, base, 65537u) != 0) {
		printf("span_longer_than_region FAIL\n");
		fails += 1;
	} else {
		printf("span_longer_than_region PASS\n");
	}
	if (span_inside(base, 65536u, base, 65536u) != 1) {
		printf("span_exact FAIL\n");
		fails += 1;
	} else {
		printf("span_exact PASS\n");
	}
	if (span_inside(base, 65536u, base + 8192u, 16384u) != 1) {
		printf("span_interior FAIL\n");
		fails += 1;
	} else {
		printf("span_interior PASS\n");
	}

	before = fake_vm_protect_calls;
	count = 0;
	begin_capture(&saved, &read_fd);
	checked_vm_protect(&count, 1, 0, 0u, (void *)base, 65536u, base - 1u, 4096, 0);
	end_capture(saved, read_fd);
	expect("protect_below", before, 0, "\"outside_region\":1");

	before = fake_vm_protect_calls;
	count = 0;
	begin_capture(&saved, &read_fd);
	checked_vm_protect(&count, 1, 0, 0u, (void *)base, 65536u,
	    base + 65536u - 4096u + 1u, 4096, 0);
	end_capture(saved, read_fd);
	expect("protect_past_end", before, 0, "\"outside_region\":1");

	before = fake_vm_protect_calls;
	count = 0;
	begin_capture(&saved, &read_fd);
	checked_vm_protect(&count, 1, 0, 0u, (void *)base, 65536u, base, 0, 0);
	end_capture(saved, read_fd);
	expect("protect_zero_length", before, 0, "\"outside_region\":1");

	before = fake_vm_protect_calls;
	count = 0;
	begin_capture(&saved, &read_fd);
	checked_vm_protect(&count, 1, 0, 0u, (void *)base, 65536u, base, -1, 0);
	end_capture(saved, read_fd);
	expect("protect_negative_length", before, 0, "\"called\":0");

	before = fake_vm_protect_calls;
	count = 0;
	begin_capture(&saved, &read_fd);
	checked_vm_protect(&count, 1, 0, 0u, (void *)base, 65536u, base + 8192u, 16384, 0);
	end_capture(saved, read_fd);
	expect("protect_interior", before, 1, "\"called\":1");
	if (fake_set_maximum != 0) {
		printf("set_maximum_false FAIL\n");
		fails += 1;
	} else {
		printf("set_maximum_false PASS\n");
	}

	before = fake_vm_protect_calls;
	count = 0;
	begin_capture(&saved, &read_fd);
	checked_vm_protect(&count, 3, 1, 0u, (void *)base, 65536u, base, 65536, 0);
	end_capture(saved, read_fd);
#if FIXTURE_ID == 16
	expect("protect_exact_owned", before, 0, "\"abi_truncation\":1");
#else
	expect("protect_exact_owned", before, 1, "\"ret\":42");
#endif
}

static void
check_narrowed_address(void)
{
	uintptr_t base;
	int before;
	int count;
	int saved;
	int read_fd;
	int want_calls;
	const char *snippet;

	base = high_base();
	if (span_inside(base, 65536u, base + 8192u, 16384u) != 1) {
		printf("high_span_inside FAIL\n");
		fails += 1;
	} else {
		printf("high_span_inside PASS\n");
	}
	before = fake_vm_protect_calls;
	count = 0;
	begin_capture(&saved, &read_fd);
	checked_vm_protect(&count, 1, 0, 8192u, (void *)base, 65536u, base + 8192u, 16384, 0);
	end_capture(saved, read_fd);
#if FIXTURE_ID == 32
	want_calls = 0;
	snippet = "\"abi_truncation\":1";
#else
	want_calls = 1;
	snippet = "\"called\":1";
#endif
	expect("protect_above_32bit", before, want_calls, snippet);
}

static void
check_narrowed_size(void)
{
#if FIXTURE_ID == 16
	uintptr_t base;
	int before;
	int count;
	int saved;
	int read_fd;

	base = low_base();
	before = fake_vm_protect_calls;
	count = 0;
	begin_capture(&saved, &read_fd);
	checked_vm_protect(&count, 3, 1, 0u, (void *)base, 65536u, base, 65536, 0);
	end_capture(saved, read_fd);
	expect("protect_length_narrowed", before, 0, "\"abi_truncation\":1");

	before = fake_vm_protect_calls;
	count = 0;
	begin_capture(&saved, &read_fd);
	checked_vm_protect(&count, 1, 0, 0u, (void *)base, 65536u, base, 4096, 0);
	end_capture(saved, read_fd);
	expect("protect_length_fits_narrow_size", before, 1, "\"called\":1");
#else
	printf("protect_length_narrowed SKIP\n");
	printf("protect_length_fits_narrow_size SKIP\n");
#endif
}

int
main(void)
{
	printf("fixture %d\n", FIXTURE_ID);
	check_ranges();
	check_narrowed_address();
	check_narrowed_size();
	printf("fails %d\n", fails);
	return fails == 0 ? 0 : 1;
}
