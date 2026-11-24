// SPDX-License-Identifier: MIT
// What a JIT program asks of the kernel, one case per child process, so that a kill of one
// case is seen and reported by the parent (docs/roadmap/issues/SF-X-claude-code.md):
//   map-jit      mmap(MAP_JIT, RWX), pthread_jit_write_protect_np() around the write, run it
//                (macOS, hardened runtime: com.apple.security.cs.allow-jit)
//   rwx-anon     mmap(RWX) without MAP_JIT, write, run
//   rw-then-rx   mmap(RW), write, mprotect(RX), run
//                (both: com.apple.security.cs.allow-unsigned-executable-memory)
//   text-write   a private writable copy of a page of the program's own __TEXT (vm_protect with
//                VM_PROT_COPY, as a debugger or a hooking library takes it), change it, back to RX, run
//                (com.apple.security.cs.disable-executable-page-protection)
//   dlopen       load a library signed by somebody else (in the parent, not in a child)
//                (com.apple.security.cs.disable-library-validation)
// The same source is signed three ways by build-jit-test.nix (sf_cs_sign.py). Output: one line
// "SFJIT <name> status=<cs status> runtime=<0|1>", one "SFJIT <name> <case> ok|FAIL|KILLED ..."
// per case, then "SFJIT <name> failed=<n>"; the exit status is the number of failed cases.
#include <dlfcn.h>
#include <errno.h>
#include <mach/mach.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MAP_JIT
#define MAP_JIT 0x0800
#endif
extern int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);
extern void sys_icache_invalidate(void *start, size_t len);

static const uint32_t ret42[] = { 0x52800540, 0xd65f03c0 };   // mov w0, #42; ret
static const uint32_t ret43 = 0x52800560;                     // mov w0, #43

__attribute__((noinline, aligned(16384), section("__TEXT,__sfjit"))) static int
patched(void)
{
	return 42;
}

static int
run(void *p)
{
	sys_icache_invalidate(p, sizeof(ret42));
	return ((int (*)(void))p)() == 42 ? 0 : 20;
}

static int
case_map_jit(void)
{
	void *p = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);

	if (p == MAP_FAILED) {
		return errno;
	}
	pthread_jit_write_protect_np(0);
	memcpy(p, ret42, sizeof(ret42));
	pthread_jit_write_protect_np(1);
	return run(p);
}

static int
case_rwx_anon(void)
{
	void *p = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);

	if (p == MAP_FAILED) {
		return errno;
	}
	memcpy(p, ret42, sizeof(ret42));
	return run(p);
}

static int
case_rw_then_rx(void)
{
	void *p = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

	if (p == MAP_FAILED) {
		return errno;
	}
	memcpy(p, ret42, sizeof(ret42));
	if (mprotect(p, 16384, PROT_READ | PROT_EXEC) != 0) {
		return errno;
	}
	return run(p);
}

static int
case_text_write(void)
{
	uint32_t *insn = (uint32_t *)(uintptr_t)patched;
	void *page = (void *)((uintptr_t)insn & ~(uintptr_t)16383);

	int (*volatile call)(void) = patched;       // through a pointer: the compiler knows what patched() returns

	if (call() != 42) {
		return 21;
	}
	if (vm_protect(mach_task_self(), (vm_address_t)page, 16384, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) {
		return EACCES;
	}
	for (int i = 0; i < 8; i++) {
		if (insn[i] == ret42[0]) {
			insn[i] = ret43;
			break;
		}
	}
	if (mprotect(page, 16384, PROT_READ | PROT_EXEC) != 0) {
		return errno;
	}
	sys_icache_invalidate(page, 16384);
	return call() == 43 ? 0 : 22;
}

static const char *library = "/System/Library/Frameworks/Security.framework/Security";   // what Bun loads

static const struct {
	const char *name;
	int (*fn)(void);
} cases[] = {
	{ "map-jit", case_map_jit }, { "rwx-anon", case_rwx_anon }, { "rw-then-rx", case_rw_then_rx },
	{ "text-write", case_text_write },
};

int
main(int argc, char **argv)
{
	const char *name = strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0];
	uint32_t status = 0;
	int failed = 0;

	if (argc > 1) {
		library = argv[1];
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	if (csops(getpid(), 0 /* CS_OPS_STATUS */, &status, sizeof(status)) != 0) {
		printf("SFJIT %s csops errno=%d\n", name, errno);
	}
	printf("SFJIT %s status=%#x runtime=%d\n", name, status, (status & 0x10000) != 0);
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		pid_t pid = fork();
		int st = 0;

		if (pid == 0) {
			int rc = cases[i].fn();
			_exit(rc == -1 ? 99 : rc > 0 && rc < 99 ? rc : rc ? 98 : 0);
		}
		if (pid < 0 || waitpid(pid, &st, 0) != pid) {
			printf("SFJIT %s %s FAIL fork/wait errno=%d\n", name, cases[i].name, errno);
			failed++;
		} else if (WIFSIGNALED(st)) {
			printf("SFJIT %s %s KILLED signal=%d\n", name, cases[i].name, WTERMSIG(st));
			failed++;
		} else if (WEXITSTATUS(st) == 99) {
			printf("SFJIT %s %s skipped (no %s)\n", name, cases[i].name, library);
		} else if (WEXITSTATUS(st) != 0) {
			printf("SFJIT %s %s FAIL code=%d (%s)\n", name, cases[i].name, WEXITSTATUS(st),
			    WEXITSTATUS(st) < 20 ? strerror(WEXITSTATUS(st)) : "wrong result");
			failed++;
		} else {
			printf("SFJIT %s %s ok\n", name, cases[i].name);
		}
	}
	// in this process: on this system dlopen() in a child of fork() fails whatever the library (seen in QEMU, run cj6)
	if (access(library, R_OK) != 0) {
		printf("SFJIT %s dlopen skipped (no %s)\n", name, library);
	} else if (dlopen(library, RTLD_LAZY | RTLD_LOCAL) != NULL) {
		printf("SFJIT %s dlopen ok\n", name);
	} else {
		printf("SFJIT %s dlopen FAIL %s\n", name, dlerror());
		failed++;
	}
	printf("SFJIT %s failed=%d\n", name, failed);
	return failed;
}
