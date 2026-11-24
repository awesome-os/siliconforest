// SPDX-License-Identifier: GPL-2.0
/*
 * Host stand-in for SFRustHello.cpp: the same Rust source (rust/sf_xnu, rust/sfrusthello),
 * compiled for the build machine, with the kernel functions it imports backed by libc.
 * It checks the logic and produces the expected log and SF_RUST_HELLO_EXPECT. It says
 * nothing about the arm64 kext: different code generator target, different allocator.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SF_RUST_HELLO_LOG(...) printf(__VA_ARGS__)
#include "sf_rust_hello.h"

static long outstanding;

void *IOMalloc(unsigned long size) { outstanding++; return malloc(size); }
void *IOMallocZero(unsigned long size) { outstanding++; return calloc(1, size); }
void IOFree(void *p, unsigned long size) { (void)size; outstanding--; free(p); }

void *
IOMallocAligned(unsigned long size, unsigned long alignment)
{
	void *p = NULL;
	if (posix_memalign(&p, alignment, size))
		return NULL;
	outstanding++;
	return p;
}

void IOFreeAligned(void *p, unsigned long size) { (void)size; outstanding--; free(p); }

void
IOLog(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void sf_rust_log(const char *text, unsigned long len) { printf("SFRustHello: %.*s\n", (int)len, text); }

void
sf_rust_panic(const char *text, unsigned long len)
{
	printf("panic: SFRustHello: rust panic: %.*s\n", (int)len, text);
	fflush(stdout);
	abort();
}

/* precompiled host core references it even with panic=abort */
void rust_eh_personality(void) { abort(); }

int
main(void)
{
	int ok;

	printf("SFRustHello: start\n");
	ok = sf_rust_hello_run();
	if (outstanding != 0) {
		printf("host: %ld allocations not freed\n", outstanding);
		ok = 0;
	}
	return ok ? 0 : 1;
}
