// SPDX-License-Identifier: MIT
// sync-probe: what the kernel gives Vulkan's synchronisation, measured on the asahi render node.
// Each step prints one line "PROBE <step> ok|FAIL ...". Steps (all through libdrm or ioctl):
//   caps            DRM_CAP_SYNCOBJ, DRM_CAP_SYNCOBJ_TIMELINE, DRM_CAP_PRIME
//   tl-signal/query/wait/wait-unsignalled/transfer   timeline syncobj ioctls
//   syncfile-export a binary syncobj's fence as a sync_file; poll() on it (must be POLLIN, not POLLNVAL)
//   syncfile-import a sync_file into another syncobj
//   syncobj-fd      syncobj as an fd and back (what Vulkan's opaque fd export uses)
//   scm-rights      the syncobj fd, the sync_file and a dma-buf sent over a socketpair and used after
//   dmabuf-export-sync / dmabuf-import-sync   DMA_BUF_IOCTL_EXPORT_SYNC_FILE / IMPORT_SYNC_FILE
// Exit status: the number of failed steps.
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <xf86drm.h>
#include "drm-uapi/asahi_drm.h"
#include "drm-uapi/dma-buf.h"

static int fails;
static void report(const char *step, int ok, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#include <stdarg.h>
static void report(const char *step, int ok, const char *fmt, ...)
{
	va_list ap;
	printf("PROBE %s %s ", step, ok ? "ok" : "FAIL");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	if (!ok)
		fails++;
}

static int open_asahi(void)
{
	for (int i = 128; i < 136; i++) {
		char p[32];
		snprintf(p, sizeof p, "/dev/dri/renderD%d", i);
		int fd = open(p, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		drmVersionPtr v = drmGetVersion(fd);
		if (v && !strcmp(v->name, "asahi")) {
			printf("PROBE node %s driver=%s %d.%d.%d\n", p, v->name, v->version_major, v->version_minor,
			       v->version_patchlevel);
			drmFreeVersion(v);
			return fd;
		}
		if (v)
			drmFreeVersion(v);
		close(fd);
	}
	return -1;
}

static const char *pollbits(short r, char *b)
{
	b[0] = 0;
	if (r & POLLIN) strcat(b, "IN|");
	if (r & POLLOUT) strcat(b, "OUT|");
	if (r & POLLERR) strcat(b, "ERR|");
	if (r & POLLHUP) strcat(b, "HUP|");
	if (r & POLLNVAL) strcat(b, "NVAL|");
	if (!b[0]) strcat(b, "none|");
	b[strlen(b) - 1] = 0;
	return b;
}

static int send_fds(int s, int *fds, int n)
{
	char cbuf[CMSG_SPACE(sizeof(int) * 4)];
	char byte = 'x';
	struct iovec iov = { &byte, 1 };
	struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = cbuf,
			    .msg_controllen = CMSG_SPACE(sizeof(int) * n) };
	memset(cbuf, 0, sizeof cbuf);
	struct cmsghdr *c = CMSG_FIRSTHDR(&m);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(sizeof(int) * n);
	memcpy(CMSG_DATA(c), fds, sizeof(int) * n);
	return sendmsg(s, &m, 0) == 1 ? 0 : -errno;
}

static int recv_fds(int s, int *fds, int n)
{
	char cbuf[CMSG_SPACE(sizeof(int) * 4)];
	char byte;
	struct iovec iov = { &byte, 1 };
	struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = cbuf, .msg_controllen = sizeof cbuf };
	if (recvmsg(s, &m, 0) != 1)
		return -errno;
	struct cmsghdr *c = CMSG_FIRSTHDR(&m);
	if (!c || c->cmsg_type != SCM_RIGHTS)
		return -EBADMSG;
	int got = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
	memcpy(fds, CMSG_DATA(c), sizeof(int) * (got < n ? got : n));
	return got;
}

int main(void)
{
	char b[64];
	int fd = open_asahi();
	if (fd < 0) {
		printf("PROBE node FAIL no asahi render node\n");
		return 99;
	}

	uint64_t so = 0, tl = 0, pr = 0;
	int r1 = drmGetCap(fd, DRM_CAP_SYNCOBJ, &so);
	int r2 = drmGetCap(fd, DRM_CAP_SYNCOBJ_TIMELINE, &tl);
	int r3 = drmGetCap(fd, DRM_CAP_PRIME, &pr);
	report("caps", !r1 && !r2 && so && tl, "syncobj=%llu (rc %d) timeline=%llu (rc %d) prime=0x%llx (rc %d)",
	       (unsigned long long)so, r1, (unsigned long long)tl, r2, (unsigned long long)pr, r3);

	uint32_t t, bin, bin2;
	if (drmSyncobjCreate(fd, 0, &t) || drmSyncobjCreate(fd, 0, &bin) || drmSyncobjCreate(fd, 0, &bin2)) {
		report("create", 0, "errno %d", errno);
		return 98;
	}
	uint64_t pt = 5, q = 0;
	int r = drmSyncobjTimelineSignal(fd, &t, &pt, 1);
	report("tl-signal", !r, "rc %d errno %d", r, r ? errno : 0);
	r = drmSyncobjQuery(fd, &t, &q, 1);
	report("tl-query", !r && q == 5, "rc %d value %llu", r, (unsigned long long)q);
	uint32_t first = 0;
	r = drmSyncobjTimelineWait(fd, &t, &pt, 1, 0, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL, &first);
	report("tl-wait", !r, "rc %d errno %d", r, r ? errno : 0);
	uint64_t pt6 = 6;
	r = drmSyncobjTimelineWait(fd, &t, &pt6, 1, 0, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, &first);
	report("tl-wait-unsignalled", r && (errno == ETIME || errno == ETIMEDOUT || errno == EINVAL),
	       "rc %d errno %d (expect ETIME/EINVAL: point 6 not submitted)", r, r ? errno : 0);
	r = drmSyncobjTransfer(fd, bin, 0, t, 5, 0);
	report("tl-transfer", !r, "rc %d errno %d", r, r ? errno : 0);

	int sf = -1;
	r = drmSyncobjExportSyncFile(fd, bin, &sf);
	struct pollfd p = { sf, POLLIN, 0 };
	int pr_ = sf >= 0 ? poll(&p, 1, 0) : -1;
	report("syncfile-export", !r && sf >= 0 && pr_ == 1 && (p.revents & POLLIN) && !(p.revents & POLLNVAL),
	       "rc %d fd %d poll=%d revents=%s", r, sf, pr_, pollbits(p.revents, b));
	r = drmSyncobjImportSyncFile(fd, bin2, sf);
	report("syncfile-import", !r, "rc %d errno %d", r, r ? errno : 0);

	int sofd = -1;
	uint32_t back = 0;
	r = drmSyncobjHandleToFD(fd, t, &sofd);
	int r4 = sofd >= 0 ? drmSyncobjFDToHandle(fd, sofd, &back) : -1;
	report("syncobj-fd", !r && !r4 && back, "export rc %d fd %d import rc %d handle %u", r, sofd, r4, back);

	// a GEM object and its dma-buf
	struct drm_asahi_gem_create gc = { .size = 16384, .flags = 0, .vm_id = 0 };
	r = drmIoctl(fd, DRM_IOCTL_ASAHI_GEM_CREATE, &gc);
	int dbuf = -1;
	int r5 = r ? -1 : drmPrimeHandleToFD(fd, gc.handle, DRM_CLOEXEC | DRM_RDWR, &dbuf);
	report("gem-prime", !r && !r5 && dbuf >= 0, "create rc %d errno %d handle %u prime rc %d fd %d", r,
	       r ? errno : 0, gc.handle, r5, dbuf);
	if (dbuf >= 0) {
		struct dma_buf_export_sync_file ex = { .flags = DMA_BUF_SYNC_RW, .fd = -1 };
		r = ioctl(dbuf, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &ex);
		struct pollfd p2 = { ex.fd, POLLIN, 0 };
		int pr2 = ex.fd >= 0 ? poll(&p2, 1, 0) : -1;
		report("dmabuf-export-sync", !r && ex.fd >= 0, "rc %d errno %d fd %d poll=%d revents=%s", r,
		       r ? errno : 0, ex.fd, pr2, pollbits(p2.revents, b));
		struct dma_buf_import_sync_file im = { .flags = DMA_BUF_SYNC_WRITE, .fd = sf };
		r = ioctl(dbuf, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &im);
		report("dmabuf-import-sync", !r, "rc %d errno %d", r, r ? errno : 0);
		struct pollfd p3 = { dbuf, POLLIN | POLLOUT, 0 };
		int pr3 = poll(&p3, 1, 0);
		report("dmabuf-poll", pr3 == 1 && !(p3.revents & POLLNVAL), "poll=%d revents=%s", pr3,
		       pollbits(p3.revents, b));
		if (ex.fd >= 0)
			close(ex.fd);
	}

	// SCM_RIGHTS: what a Wayland client does with these descriptors
	int sp[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
		int out[3] = { sofd, sf, dbuf }, in[3] = { -1, -1, -1 };
		int n = (dbuf >= 0) ? 3 : 2;
		int rs = send_fds(sp[0], out, n);
		int rr = rs ? rs : recv_fds(sp[1], in, n);
		uint32_t h2 = 0, h3 = 0;
		int a = in[0] >= 0 ? drmSyncobjFDToHandle(fd, in[0], &h2) : -1;
		struct pollfd p4 = { in[1], POLLIN, 0 };
		int pp = in[1] >= 0 ? poll(&p4, 1, 0) : -1;
		int c = (n == 3 && in[2] >= 0) ? drmPrimeFDToHandle(fd, in[2], &h3) : -1;
		report("scm-rights", rs == 0 && rr == n && !a && pp == 1 && (n < 3 || (!c && h3 == gc.handle)),
		       "send %d recv %d; syncobj fd import rc %d; sync_file poll=%d revents=%s; dma-buf import rc %d handle %u (expect %u)",
		       rs, rr, a, pp, pollbits(p4.revents, b), c, h3, gc.handle);
	} else {
		report("scm-rights", 0, "socketpair errno %d", errno);
	}
	printf("PROBE done failures=%d\n", fails);
	return fails;
}
