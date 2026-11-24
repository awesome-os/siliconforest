// SPDX-License-Identifier: MIT
// LD_PRELOAD interposer (Linux builder only): logs every ioctl and every fd-backed mmap the
// process makes, in call order, before passing them on (to Mesa's asahi drm-shim, preloaded
// after this one, or to the kernel).  Output goes to the file named by AGX_IOCTL_TRACE.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>

static FILE *out;
static void init(void)
{
	if (!out) {
		const char *p = getenv("AGX_IOCTL_TRACE");
		out = p ? fopen(p, "w") : stderr;
		if (!out)
			out = stderr;
		setvbuf(out, NULL, _IOLBF, 0);
	}
}

static const char *name(unsigned long req)
{
	static const struct { unsigned type, nr; const char *n; } t[] = {
		{ 'd', 0x00, "DRM_IOCTL_VERSION" }, { 'd', 0x01, "DRM_IOCTL_GET_UNIQUE" },
		{ 'd', 0x09, "DRM_IOCTL_GEM_CLOSE" }, { 'd', 0x0c, "DRM_IOCTL_GET_CAP" },
		{ 'd', 0x0d, "DRM_IOCTL_SET_CLIENT_CAP" },
		{ 'd', 0x2d, "DRM_IOCTL_PRIME_HANDLE_TO_FD" }, { 'd', 0x2e, "DRM_IOCTL_PRIME_FD_TO_HANDLE" },
		{ 'd', 0xbf, "DRM_IOCTL_SYNCOBJ_CREATE" }, { 'd', 0xc0, "DRM_IOCTL_SYNCOBJ_DESTROY" },
		{ 'd', 0xc1, "DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD" }, { 'd', 0xc2, "DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE" },
		{ 'd', 0xc3, "DRM_IOCTL_SYNCOBJ_WAIT" }, { 'd', 0xc4, "DRM_IOCTL_SYNCOBJ_RESET" },
		{ 'd', 0xc5, "DRM_IOCTL_SYNCOBJ_SIGNAL" }, { 'd', 0xca, "DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT" },
		{ 'd', 0xcb, "DRM_IOCTL_SYNCOBJ_QUERY" }, { 'd', 0xcc, "DRM_IOCTL_SYNCOBJ_TRANSFER" },
		{ 'd', 0xcd, "DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL" },
		{ 'd', 0x40, "DRM_IOCTL_ASAHI_GET_PARAMS" }, { 'd', 0x41, "DRM_IOCTL_ASAHI_GET_TIME" },
		{ 'd', 0x42, "DRM_IOCTL_ASAHI_VM_CREATE" }, { 'd', 0x43, "DRM_IOCTL_ASAHI_VM_DESTROY" },
		{ 'd', 0x44, "DRM_IOCTL_ASAHI_VM_BIND" }, { 'd', 0x45, "DRM_IOCTL_ASAHI_GEM_CREATE" },
		{ 'd', 0x46, "DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET" }, { 'd', 0x47, "DRM_IOCTL_ASAHI_GEM_BIND_OBJECT" },
		{ 'd', 0x48, "DRM_IOCTL_ASAHI_QUEUE_CREATE" }, { 'd', 0x49, "DRM_IOCTL_ASAHI_QUEUE_DESTROY" },
		{ 'd', 0x4a, "DRM_IOCTL_ASAHI_SUBMIT" }, { 'd', 0x4b, "DRM_IOCTL_ASAHI_GEM_MADVISE" },
		{ 'b', 0x00, "DMA_BUF_IOCTL_SYNC" }, { 'b', 0x02, "DMA_BUF_IOCTL_EXPORT_SYNC_FILE" },
		{ 'b', 0x03, "DMA_BUF_IOCTL_IMPORT_SYNC_FILE" },
		{ '>', 0x03, "SYNC_IOC_MERGE" }, { '>', 0x04, "SYNC_IOC_FILE_INFO" },
	};
	for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (t[i].type == _IOC_TYPE(req) && t[i].nr == _IOC_NR(req))
			return t[i].n;
	return "?";
}

static void detail(unsigned long req, const void *arg, char *buf, size_t n)
{
	const uint32_t *w = arg;
	const uint64_t *q = arg;
	buf[0] = 0;
	if (!arg || _IOC_TYPE(req) != 'd')
		return;
	switch (_IOC_NR(req)) {
	case 0x0c: snprintf(buf, n, " cap=%llu", (unsigned long long)q[0]); break;
	case 0x40: snprintf(buf, n, " group=%u size=%llu", w[0], (unsigned long long)q[2]); break;
	case 0x44: {
		// first bind op: flags, handle, offset, range, addr
		const uint32_t *o = (const uint32_t *)(uintptr_t)q[2];
		const uint64_t *oq = (const uint64_t *)(uintptr_t)q[2];
		snprintf(buf, n, " vm=%u num_binds=%u stride=%u op0{flags=0x%x handle=%u offset=0x%llx range=0x%llx addr=0x%llx}",
		    w[0], w[1], w[2], o[0], o[1], (unsigned long long)oq[1], (unsigned long long)oq[2],
		    (unsigned long long)oq[3]);
		break;
	}
	case 0x45: snprintf(buf, n, " size=0x%llx flags=0x%x vm=%u", (unsigned long long)q[0], w[2], w[3]); break;
	case 0x47: snprintf(buf, n, " op=%u flags=0x%x handle=%u vm=%u", w[0], w[1], w[2], w[3]); break;
	case 0x48: snprintf(buf, n, " flags=0x%x vm=%u prio=%u", w[0], w[1], w[2]); break;
	case 0x4a: {
		// walk the command buffer: { u16 type, u16 size, u16 vdm_barrier, u16 cdm_barrier } + payload
		static const char *const types[] = { "RENDER", "COMPUTE", "SET_VERTEX_ATTACHMENTS",
			"SET_FRAGMENT_ATTACHMENTS", "SET_COMPUTE_ATTACHMENTS" };
		const uint8_t *cb = (const uint8_t *)(uintptr_t)q[1];
		int len = snprintf(buf, n, " queue=%u in_syncs=%u out_syncs=%u cmdbuf_size=%u cmds=[", w[5], w[6], w[7], w[8]);
		for (uint32_t off = 0; off + 8 <= w[8] && len < (int)n - 40;) {
			const uint16_t *h = (const uint16_t *)(cb + off);
			len += snprintf(buf + len, n - len, "%s%s:%u", off ? " " : "", h[0] < 5 ? types[h[0]] : "?", h[1]);
			off += 8 + h[1];
		}
		snprintf(buf + len, n - len, "]");
		break;
	}
	case 0xbf: snprintf(buf, n, " flags=0x%x", w[1]); break;
	case 0xc1: case 0xc2: snprintf(buf, n, " flags=0x%x%s", w[1], (w[1] & 1) ? "(SYNC_FILE)" : ""); break;
	case 0xc3: snprintf(buf, n, " count=%u timeout=%lld flags=0x%x", w[4], (long long)q[1], w[5]); break;
	}
}

int ioctl(int fd, unsigned long req, ...)
{
	static int (*real)(int, unsigned long, ...);
	va_list ap;
	void *arg;
	char d[224];
	int ret, e;

	if (!real)
		real = dlsym(RTLD_NEXT, "ioctl");
	init();
	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	detail(req, arg, d, sizeof(d));
	ret = real(fd, req, arg);
	e = errno;
	// The shim answers SYNCOBJ_HANDLE_TO_FD(EXPORT_SYNC_FILE) with success but no fd, and Mesa
	// then skips the fence path a real kernel takes.  With AGX_IOCTL_TRACE_FAKE_SYNC_FILE=1 hand
	// back a placeholder fd so the rest of that path (create, import, wait, destroy) is recorded.
	if (ret == 0 && arg && _IOC_TYPE(req) == 'd' && _IOC_NR(req) == 0xc1 &&
	    (((uint32_t *)arg)[1] & 1) && ((int32_t *)arg)[2] < 0 && getenv("AGX_IOCTL_TRACE_FAKE_SYNC_FILE")) {
		((int32_t *)arg)[2] = open("/dev/null", O_RDONLY | O_CLOEXEC);
		strncat(d, " [tracer supplied a placeholder sync-file fd]", sizeof(d) - strlen(d) - 1);
	}
	fprintf(out, "ioctl fd=%d 0x%08lx dir=%s%s size=%lu %s%s -> %d%s%s\n", fd, req,
	    (_IOC_DIR(req) & _IOC_WRITE) ? "W" : "", (_IOC_DIR(req) & _IOC_READ) ? "R" : "",
	    (unsigned long)_IOC_SIZE(req), name(req), d, ret, ret ? " errno=" : "", ret ? strerror(e) : "");
	errno = e;
	return ret;
}

static void log_mmap(void *addr, size_t len, int prot, int flags, int fd, long long off, void *ret)
{
	int e = errno;
	init();
	if (fd >= 0)
		fprintf(out, "mmap fd=%d addr=%p len=0x%zx prot=0x%x flags=0x%x%s offset=0x%llx -> %s\n", fd, addr,
		    len, prot, flags, (flags & MAP_FIXED) ? "(FIXED)" : "", off, ret == MAP_FAILED ? "FAILED" : "ok");
	errno = e;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	static void *(*real)(void *, size_t, int, int, int, off_t);
	if (!real)
		real = dlsym(RTLD_NEXT, "mmap");
	void *r = real(addr, len, prot, flags, fd, off);
	log_mmap(addr, len, prot, flags, fd, off, r);
	return r;
}

void *mmap64(void *addr, size_t len, int prot, int flags, int fd, off64_t off)
{
	static void *(*real)(void *, size_t, int, int, int, off64_t);
	if (!real)
		real = dlsym(RTLD_NEXT, "mmap64");
	void *r = real(addr, len, prot, flags, fd, off);
	log_mmap(addr, len, prot, flags, fd, off, r);
	return r;
}
