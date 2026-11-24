// SPDX-License-Identifier: MIT
/*
 * dcp-flip-test - KMS through /dev/dri/card0 on the display coprocessor driver (SFDcp).
 *
 * Prints the connector, mode, CRTC and planes; creates two dumb buffers of the mode's size,
 * fills each with a solid colour and a nonce pattern; one atomic commit with ALLOW_MODESET
 * to the first; then page-flips between the two for --seconds (default 2), counting
 * flip-complete events and printing the rate and the shortest and longest interval.
 * Events are taken with a blocking read() on the DRM fd (select() on it is not relied on).
 * Exit status 0 only if every step worked; a failure names the call and errno.
 *
 *   dcp-flip-test [--device /dev/dri/cardN] [--seconds N] [--nonce HEX] [--no-flip]
 *                 [--prime-from auto|/dev/dri/renderDN] [--wait read|poll|select|kqueue]
 *                 [--scm-rights] [--fence poll|select|kqueue] [--dmabuf-mmap]
 *                 [--in-fence] [--dmabuf-ioctls] [--dpms-cycles N] [--dpms-off-ms MS]
 *
 * Without --device the display is the card node whose DRM driver is "apple"; --prime-from
 * auto is the render node whose driver is "asahi".
 *
 * --prime-from: the two buffers are not dumb buffers of the display driver but GEM objects
 * of the GPU driver (asahi GEM_CREATE on the render node), filled through the render node's
 * mapping, exported as PRIME fds and imported into the card node: the display then scans
 * out memory that belongs to the GPU driver, with no copy. This tests the cross-driver
 * import; the pixels are written by the CPU here, not rendered.
 *
 * --scm-rights (with --prime-from): the GPU buffers are made by a second process. The test
 * forks; the child opens the render node, creates, fills and exports the two buffers and
 * sends the two dma-buf descriptors over a Unix socket (SCM_RIGHTS), closing its own copies.
 * The parent imports what it received into the card node and flips. At the end the child
 * reads the buffers back through its mapping and reports, then exits, and the parent flips
 * ten more times: the buffers are then kept alive by the display driver's import alone.
 *
 * --dmabuf-mmap: each buffer's dma-buf descriptor is mmap()ed read/write and used between
 * DMA_BUF_IOCTL_SYNC start and end: what the buffer's owner wrote is read through it, then
 * a pattern is written into rows 48..63 through it, and the owner reads the pattern back
 * through its own mapping (the dumb buffer's, or the render node's). Without --prime-from
 * the descriptor is the display driver's (exported from the card node, same process); with
 * --prime-from auto --scm-rights it is the GPU driver's, mapped in the process that
 * received it over the socket and read back by the process that made the buffer. Also
 * checked: a mapping longer than the buffer is refused.
 *
 * --dpms-cycles N: display sleep the way a compositor does it. After the normal flip run, N
 * times: an atomic commit with the CRTC's ACTIVE = 0 and ALLOW_MODESET (the driver powers the
 * panel down), a pause of --dpms-off-ms (default 2000), a commit with ACTIVE = 1 (and a flip
 * event), then half a second of flips. Printed per cycle: how long each commit took, the time
 * to the first flip event after waking, and the flip rate. Fails if a commit fails or the
 * rate after a wake is under 50 per second.
 *
 * --dmabuf-ioctls (with --prime-from): DMA_BUF_IOCTL_SYNC (start and end), EXPORT_SYNC_FILE
 * and IMPORT_SYNC_FILE on each GPU buffer's dma-buf descriptor, in the process that created
 * the buffer (what Mesa does with its own back buffer at every swap) and, with --scm-rights,
 * also in the process that received the descriptor. --dmabuf-mmap implies it.
 *
 * --in-fence (with --prime-from): every flip exports the sync_file of the GPU buffer it flips
 * to (DMA_BUF_IOCTL_EXPORT_SYNC_FILE on the GPU driver's dma-buf, so the sync_file is the GPU
 * driver's) and gives it to the display driver as the plane's IN_FENCE_FD. Nothing renders
 * here, so the fence is already signalled: this tests that the display driver accepts the
 * other driver's sync_file, not that it waits for one.
 *
 * --fence: every flip asks for the CRTC's OUT_FENCE_PTR, a sync_file descriptor that signals
 * when the flip completes, and waits for it with poll(), select() or kevent() (1 s timeout)
 * before reading the event. The fence is also imported into the other buffer's dma-buf
 * (DMA_BUF_IOCTL_IMPORT_SYNC_FILE, write) and that descriptor is waited on the same way,
 * then exported again (EXPORT_SYNC_FILE) and checked to be signalled. Counted: fences that
 * were still pending right after the commit, timeouts, and the time from commit to wakeup.
 * Fails on any timeout, and if no fence was ever seen pending (then no wakeup was tested).
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <asahi_drm.h>	/* the pin's include/uapi/drm, see dcp-flip-test.nix */

#define P "dcp-flip-test: "

/*
 * Every line goes to stdout and, unbuffered, to /dev/console: if the kernel panics in one of
 * the calls below, the console still shows the last step that completed (boot ft02).
 */
static int console_fd = -1;

static void out(const char *fmt, ...)
{
	char line[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (n >= (int)sizeof(line))
		n = sizeof(line) - 1;
	fwrite(line, 1, (size_t)n, stdout);
	if (console_fd >= 0)
		(void)!write(console_fd, line, (size_t)n);
}

static int fail(const char *what)
{
	int e = errno;

	out(P "FAIL %s: errno %d (%s)\n", what, e, strerror(e));
	return 1;
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint32_t prop_id(int fd, uint32_t obj, uint32_t type, const char *name, uint64_t *value)
{
	drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj, type);
	uint32_t id = 0;

	if (!props)
		return 0;
	for (uint32_t i = 0; i < props->count_props && !id; i++) {
		drmModePropertyRes *p = drmModeGetProperty(fd, props->props[i]);

		if (p && !strcmp(p->name, name)) {
			id = p->prop_id;
			if (value)
				*value = props->prop_values[i];
		}
		drmModeFreeProperty(p);
	}
	drmModeFreeObjectProperties(props);
	if (!id)
		out(P "object %u has no property %s\n", obj, name);
	return id;
}

/*
 * How the test learns that the fd is readable before it reads the event:
 *   read    a blocking read() and nothing else
 *   poll    poll(POLLIN), select: select(), kqueue: kevent(EVFILT_READ), each with a 1 s
 *           timeout. A timeout is counted and reported, and the read() still follows, so a
 *           wait that never wakes shows as timeouts and about one flip per second.
 */
enum { WAIT_READ, WAIT_POLL, WAIT_SELECT, WAIT_KQUEUE };
static int wait_mode = WAIT_READ, wait_kq = -1;
static unsigned wait_timeouts, wait_errors;

static void wait_readable(int fd)
{
	int r = 1;

	if (wait_mode == WAIT_POLL) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };

		r = poll(&pfd, 1, 1000);
		if (r > 0 && !(pfd.revents & POLLIN)) {
			if (!wait_errors)
				out(P "poll: revents 0x%x without POLLIN\n", pfd.revents);
			wait_errors++;
			return;
		}
	} else if (wait_mode == WAIT_SELECT) {
		fd_set rfds;
		struct timeval tv = { 1, 0 };

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		r = select(fd + 1, &rfds, NULL, NULL, &tv);
	} else if (wait_mode == WAIT_KQUEUE) {
		struct kevent ev;
		struct timespec ts = { 1, 0 };

		r = kevent(wait_kq, NULL, 0, &ev, 1, &ts);
	} else {
		return;
	}
	if (r == 0)
		wait_timeouts++;
	else if (r < 0) {
		if (!wait_errors)
			out(P "wait for the fd failed: errno %d (%s)\n", errno, strerror(errno));
		wait_errors++;
	}
}

/* The kernel's timestamp in the last flip-complete event, in microseconds. */
static uint64_t event_us;

/* Waits (see above), then read()s until a flip-complete event arrives. 0, or 1 after printing the failure. */
static int wait_flip(int fd, const char *what)
{
	char evbuf[256];

	for (;;) {
		ssize_t n;

		wait_readable(fd);
		n = read(fd, evbuf, sizeof(evbuf));

		if (n < 0 && errno == EINTR)
			continue;
		if (n < (ssize_t)sizeof(struct drm_event)) {
			out(P "%s: read returned %zd: ", what, n);
			return fail("read of the DRM event");
		}
		for (ssize_t off = 0; off + (ssize_t)sizeof(struct drm_event) <= n;) {
			struct drm_event *ev = (struct drm_event *)(evbuf + off);

			if (ev->type == DRM_EVENT_FLIP_COMPLETE) {
				if (ev->length >= sizeof(struct drm_event_vblank)) {
					struct drm_event_vblank *vb = (struct drm_event_vblank *)ev;

					event_us = (uint64_t)vb->tv_sec * 1000000ull + vb->tv_usec;
				}
				return 0;
			}
			if (ev->length < sizeof(*ev))
				break;
			off += ev->length;
		}
	}
}

/*
 * include/uapi/linux/dma-buf.h at the pin. The numbers are built with this system's _IOWR,
 * as the kernel side's are.
 */
struct dma_buf_export_sync_file { uint32_t flags; int32_t fd; };
struct dma_buf_import_sync_file { uint32_t flags; int32_t fd; };
#define DMA_BUF_SYNC_READ	(1 << 0)
#define DMA_BUF_SYNC_WRITE	(2 << 0)
struct dma_buf_sync { uint64_t flags; };
#define DMA_BUF_SYNC_RW		(DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START	(0 << 2)
#define DMA_BUF_SYNC_END	(1 << 2)
#define DMA_BUF_IOCTL_SYNC	_IOW('b', 0, struct dma_buf_sync)
#define DMA_BUF_IOCTL_EXPORT_SYNC_FILE	_IOWR('b', 2, struct dma_buf_export_sync_file)
#define DMA_BUF_IOCTL_IMPORT_SYNC_FILE	_IOW('b', 3, struct dma_buf_import_sync_file)

enum { FENCE_NONE, FENCE_POLL, FENCE_SELECT, FENCE_KQUEUE };
static int fence_mode = FENCE_NONE;

/* 1 if fd becomes readable within timeout_ms, 0 on timeout, -1 on error (errno set). */
static int fd_readable(int fd, int how, int timeout_ms)
{
	if (how == FENCE_SELECT) {
		fd_set rfds;
		struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		return select(fd + 1, &rfds, NULL, NULL, &tv);
	}
	if (how == FENCE_KQUEUE) {
		struct kevent ev, got;
		struct timespec ts = { timeout_ms / 1000, (long)(timeout_ms % 1000) * 1000000 };
		int kq = kqueue(), r, e;

		if (kq < 0)
			return -1;
		EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
		r = kevent(kq, &ev, 1, &got, 1, &ts);
		e = errno;
		if (r > 0 && (got.flags & EV_ERROR)) {
			e = (int)got.data;
			r = -1;
		}
		close(kq);
		errno = e;
		return r;
	}
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int r = poll(&pfd, 1, timeout_ms);

	if (r > 0 && !(pfd.revents & POLLIN)) {
		errno = EIO;
		return -1;
	}
	return r;
}

/*
 * Nodes are found by driver name, never by number: with both drivers loaded card0 is the GPU
 * (boot cb02). Scans /dev/dri for entries starting with `prefix` whose DRM driver is `driver`
 * and returns an open fd, with the path in `path`. -1 if there is none.
 */
static int open_by_driver(const char *prefix, const char *driver, char *path, size_t path_len)
{
	DIR *d = opendir("/dev/dri");
	struct dirent *e;
	int found = -1;

	if (!d)
		return -1;
	while (found < 0 && (e = readdir(d)) != NULL) {
		drmVersion *v;
		int fd;

		if (strncmp(e->d_name, prefix, strlen(prefix)))
			continue;
		snprintf(path, path_len, "/dev/dri/%s", e->d_name);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		v = drmGetVersion(fd);
		out(P "%s: driver %s\n", path, v ? v->name : "(no version)");
		if (v && !strcmp(v->name, driver))
			found = fd;
		else
			close(fd);
		drmFreeVersion(v);
	}
	closedir(d);
	return found;
}

static int in_fence;
static int dmabuf_ioctls;
static int dmabuf_ioctl_check(int dfd, const char *who);

struct buf {
	int dfd;	/* --in-fence: the GPU driver's dma-buf descriptor, else unused */
	uint32_t handle, pitch, fb;
	uint64_t size;
	uint32_t *map;
};

/* A GPU-driver buffer: created and mapped on the render node, imported into the card node. */
static int make_prime_buf(int fd, int render_fd, struct buf *b, uint32_t w, uint32_t h)
{
	struct drm_asahi_gem_create create = { 0 };
	struct drm_asahi_gem_mmap_offset mo = { 0 };
	int prime_fd = -1;

	b->pitch = w * 4;
	b->size = ((uint64_t)b->pitch * h + 0x3fff) & ~0x3fffull;
	create.size = b->size;
	out(P "step: asahi GEM_CREATE %" PRIu64 " bytes on the render node\n", b->size);
	if (drmIoctl(render_fd, DRM_IOCTL_ASAHI_GEM_CREATE, &create))
		return fail("DRM_IOCTL_ASAHI_GEM_CREATE");
	mo.handle = create.handle;
	if (drmIoctl(render_fd, DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET, &mo))
		return fail("DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET");
	out(P "step: mmap of the GPU buffer through the render node\n");
	b->map = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, render_fd, (off_t)mo.offset);
	if (b->map == MAP_FAILED)
		return fail("mmap of the GPU buffer");
	out(P "step: PRIME export from the render node (handle %u)\n", create.handle);
	if (drmPrimeHandleToFD(render_fd, create.handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd))
		return fail("drmPrimeHandleToFD (render node)");
	if (dmabuf_ioctls && dmabuf_ioctl_check(prime_fd, "in the process that created the buffer"))
		return 1;
	if (fd < 0) {
		/* --scm-rights, in the exporting process: the descriptor is sent, not imported */
		b->handle = (uint32_t)prime_fd;
		return 0;
	}
	out(P "step: PRIME import into the card node (fd %d)\n", prime_fd);
	if (drmPrimeFDToHandle(fd, prime_fd, &b->handle))
		return fail("drmPrimeFDToHandle (card node)");
	if (in_fence)
		b->dfd = prime_fd;
	else
		close(prime_fd);
	out(P "GPU buffer: render handle %u -> card handle %u\n", create.handle, b->handle);
	return 0;
}

static void fill_buf(struct buf *b, uint32_t w, uint32_t h, uint32_t colour, uint32_t nonce);
static int add_fb(int fd, struct buf *b, uint32_t w, uint32_t h, uint32_t colour);

static int make_buf(int fd, int render_fd, struct buf *b, uint32_t w, uint32_t h, uint32_t colour, uint32_t nonce)
{
	struct drm_mode_create_dumb create = { .width = w, .height = h, .bpp = 32 };
	struct drm_mode_map_dumb map = { 0 };

	if (render_fd >= 0) {
		if (make_prime_buf(fd, render_fd, b, w, h))
			return 1;
		goto fill;
	}
	out(P "step: CREATE_DUMB %ux%u\n", w, h);
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create))
		return fail("DRM_IOCTL_MODE_CREATE_DUMB");
	b->handle = create.handle;
	b->pitch = create.pitch;
	b->size = create.size;
	map.handle = b->handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map))
		return fail("DRM_IOCTL_MODE_MAP_DUMB");
	out(P "step: mmap %" PRIu64 " bytes at offset 0x%" PRIx64 "\n", b->size, (uint64_t)map.offset);
	b->map = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)map.offset);
	if (b->map == MAP_FAILED)
		return fail("mmap of the dumb buffer");
fill:
	fill_buf(b, w, h, colour, nonce);
	return add_fb(fd, b, w, h, colour);
}

static void fill_buf(struct buf *b, uint32_t w, uint32_t h, uint32_t colour, uint32_t nonce)
{
	for (uint32_t y = 0; y < h; y++) {
		uint32_t *row = (uint32_t *)((uint8_t *)b->map + (size_t)y * b->pitch);

		for (uint32_t x = 0; x < w; x++)
			row[x] = colour;
	}
	/* the nonce as 32 squares of 32x32 pixels along the top: white for 1, black for 0 */
	for (uint32_t bit = 0; bit < 32 && (bit + 1) * 32 <= w && h >= 64; bit++)
		for (uint32_t y = 16; y < 48; y++) {
			uint32_t *row = (uint32_t *)((uint8_t *)b->map + (size_t)y * b->pitch);

			for (uint32_t x = bit * 32; x < bit * 32 + 32; x++)
				row[x] = (nonce >> (31 - bit)) & 1 ? 0x00ffffff : 0x00000000;
		}
}

static int add_fb(int fd, struct buf *b, uint32_t w, uint32_t h, uint32_t colour)
{
	uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };

	handles[0] = b->handle;
	pitches[0] = b->pitch;
	out(P "step: ADDFB2\n");
	if (drmModeAddFB2(fd, w, h, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &b->fb, 0))
		return fail("drmModeAddFB2 (XRGB8888)");
	out(P "buffer handle %u pitch %u size %" PRIu64 " fb %u colour 0x%06x\n", b->handle, b->pitch,
	       b->size, b->fb, colour);
	return 0;
}


static uint32_t count_bad(const struct buf *b, uint32_t w, uint32_t h, uint32_t want)
{
	uint32_t bad = 0;

	for (uint32_t y = 64; y < h; y++) {
		const uint32_t *row = (const uint32_t *)((const uint8_t *)b->map + (size_t)y * b->pitch);

		for (uint32_t x = 0; x < w; x++)
			bad += row[x] != want;
	}
	return bad;
}

/* --dmabuf-mmap */
static int dmabuf_mmap;

/* The three ioctls Mesa uses on a dma-buf. 0, or 1 after printing which one failed. */
static int dmabuf_ioctl_check(int dfd, const char *who)
{
	struct dma_buf_sync sync = { .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW };
	struct dma_buf_export_sync_file ex = { .flags = DMA_BUF_SYNC_RW, .fd = -1 };
	struct dma_buf_import_sync_file imp = { .flags = DMA_BUF_SYNC_WRITE, .fd = -1 };

	if (ioctl(dfd, DMA_BUF_IOCTL_SYNC, &sync)) {
		out(P "dma-buf %d, %s: ", dfd, who);
		return fail("DMA_BUF_IOCTL_SYNC (start)");
	}
	sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
	if (ioctl(dfd, DMA_BUF_IOCTL_SYNC, &sync)) {
		out(P "dma-buf %d, %s: ", dfd, who);
		return fail("DMA_BUF_IOCTL_SYNC (end)");
	}
	if (ioctl(dfd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &ex)) {
		out(P "dma-buf %d, %s: ", dfd, who);
		return fail("DMA_BUF_IOCTL_EXPORT_SYNC_FILE");
	}
	imp.fd = ex.fd;
	if (ioctl(dfd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &imp)) {
		out(P "dma-buf %d, %s: ", dfd, who);
		return fail("DMA_BUF_IOCTL_IMPORT_SYNC_FILE");
	}
	close(ex.fd);
	out(P "dma-buf %d, %s: SYNC, EXPORT_SYNC_FILE and IMPORT_SYNC_FILE succeeded\n", dfd, who);
	return 0;
}

static uint32_t pattern(int k, uint32_t x, uint32_t y)
{
	return ((x * 2654435761u) ^ (y * 40503u) ^ ((uint32_t)k * 0x9e3779b9u)) & 0x00ffffff;
}

/* Pixels of rows 48..63 that are not the pattern, read through the owner's mapping. */
static uint32_t pattern_bad(const struct buf *b, int k, uint32_t w)
{
	uint32_t bad = 0;

	for (uint32_t y = 48; y < 64; y++) {
		const uint32_t *row = (const uint32_t *)((const uint8_t *)b->map + (size_t)y * b->pitch);

		for (uint32_t x = 0; x < w; x++)
			bad += row[x] != pattern(k, x, y);
	}
	return bad;
}

/*
 * Maps the dma-buf descriptor, checks that rows 64.. hold `colour` (what the owner wrote),
 * writes the pattern into rows 48..63, unmaps. 0, or 1 after printing the failure.
 */
static int dmabuf_map_and_write(int dfd, int k, uint32_t w, uint32_t h, uint32_t pitch, uint64_t size, uint32_t colour)
{
	struct dma_buf_sync sync = { .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW };
	uint32_t *m, wrong = 0;
	void *over;

	over = mmap(NULL, size + 0x4000, PROT_READ | PROT_WRITE, MAP_SHARED, dfd, 0);
	if (over != MAP_FAILED) {
		out(P "FAIL a mapping of %" PRIu64 " bytes of a %" PRIu64 "-byte dma-buf was accepted\n", size + 0x4000, size);
		return 1;
	}
	out(P "dma-buf %d: a mapping longer than the buffer is refused, errno %d\n", dfd, errno);
	out(P "step: mmap of dma-buf descriptor %d, %" PRIu64 " bytes read/write\n", dfd, size);
	m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, dfd, 0);
	if (m == MAP_FAILED)
		return fail("mmap of the dma-buf descriptor");
	if (ioctl(dfd, DMA_BUF_IOCTL_SYNC, &sync))
		return fail("DMA_BUF_IOCTL_SYNC (start, read/write)");
	for (uint32_t y = 64; y < h; y++) {
		const uint32_t *row = (const uint32_t *)((const uint8_t *)m + (size_t)y * pitch);

		for (uint32_t x = 0; x < w; x++)
			wrong += row[x] != colour;
	}
	for (uint32_t y = 48; y < 64; y++) {
		uint32_t *row = (uint32_t *)((uint8_t *)m + (size_t)y * pitch);

		for (uint32_t x = 0; x < w; x++)
			row[x] = pattern(k, x, y);
	}
	sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
	if (ioctl(dfd, DMA_BUF_IOCTL_SYNC, &sync))
		return fail("DMA_BUF_IOCTL_SYNC (end, read/write)");
	sync.flags = 1 << 5;
	if (ioctl(dfd, DMA_BUF_IOCTL_SYNC, &sync) == 0 || errno != EINVAL) {
		out(P "FAIL DMA_BUF_IOCTL_SYNC with an unknown flag did not fail with EINVAL (errno %d)\n", errno);
		return 1;
	}
	if (munmap(m, size))
		return fail("munmap of the dma-buf mapping");
	out(P "dma-buf %d (buffer %d): %u pixels read through the descriptor's mapping differ from 0x%06x%s; "
	    "pattern written to rows 48..63\n", dfd, k, wrong, colour, wrong ? " FAILED" : "");
	return wrong != 0;
}

/* --scm-rights: what travels with the two descriptors, and the child's answer at the end */
struct scm_msg {
	uint32_t pitch[2];
	uint64_t size[2];
};

/* The exporting process. Does not return. */
static void scm_child(int sock, uint32_t w, uint32_t h, uint32_t nonce)
{
	static const uint32_t colours[2] = { 0x00203080, 0x00804020 };
	struct buf b[2] = { { 0 }, { 0 } };
	struct scm_msg m = { { 0 }, { 0 } };
	struct iovec iov = { &m, sizeof(m) };
	union {
		char buf[CMSG_SPACE(2 * sizeof(int))];
		struct cmsghdr align;
	} u;
	struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = u.buf, .msg_controllen = sizeof(u.buf) };
	struct cmsghdr *c;
	char rpath[64] = "", cmd;
	uint32_t bad[4] = { 0, 0, 0, 0 };
	int fds[2], render_fd;

	render_fd = open_by_driver("renderD", "asahi", rpath, sizeof(rpath));
	if (render_fd < 0) {
		out(P "child: FAIL no /dev/dri/renderD* with DRM driver \"asahi\"\n");
		_exit(2);
	}
	out(P "child %d: buffers come from the GPU driver on %s\n", (int)getpid(), rpath);
	for (int k = 0; k < 2; k++) {
		if (make_prime_buf(-1, render_fd, &b[k], w, h))
			_exit(2);
		fds[k] = (int)b[k].handle;
		fill_buf(&b[k], w, h, colours[k], k ? ~nonce : nonce);
		m.pitch[k] = b[k].pitch;
		m.size[k] = b[k].size;
	}
	memset(&u, 0, sizeof(u));
	c = CMSG_FIRSTHDR(&mh);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(2 * sizeof(int));
	memcpy(CMSG_DATA(c), fds, 2 * sizeof(int));
	out(P "child: step: sendmsg with SCM_RIGHTS, descriptors %d and %d\n", fds[0], fds[1]);
	if (sendmsg(sock, &mh, 0) != (ssize_t)sizeof(m)) {
		fail("child: sendmsg (SCM_RIGHTS, two dma-buf descriptors)");
		_exit(3);
	}
	close(fds[0]);
	close(fds[1]);
	out(P "child: sent and closed both descriptors\n");
	/* the parent asks for the readback when its flips are done */
	if (read(sock, &cmd, 1) != 1) {
		fail("child: read of the parent's request");
		_exit(3);
	}
	for (int k = 0; k < 2; k++) {
		bad[k] = count_bad(&b[k], w, h, colours[k]);
		if (dmabuf_mmap)
			bad[2 + k] = pattern_bad(&b[k], k, w);
	}
	if (write(sock, bad, sizeof(bad)) != (ssize_t)sizeof(bad))
		_exit(3);
	_exit(bad[0] || bad[1] || bad[2] || bad[3] ? 4 : 0);
}

static int scm_receive(int sock, int fds[2], struct scm_msg *m)
{
	struct iovec iov = { m, sizeof(*m) };
	union {
		char buf[CMSG_SPACE(2 * sizeof(int))];
		struct cmsghdr align;
	} u;
	struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = u.buf, .msg_controllen = sizeof(u.buf) };
	struct cmsghdr *c;
	ssize_t n;

	do
		n = recvmsg(sock, &mh, 0);
	while (n < 0 && errno == EINTR);
	if (n != (ssize_t)sizeof(*m)) {
		out(P "recvmsg returned %zd (the exporting process failed before sending?): ", n);
		return fail("recvmsg from the exporting process");
	}
	c = CMSG_FIRSTHDR(&mh);
	if (!c || c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS ||
	    c->cmsg_len != CMSG_LEN(2 * sizeof(int))) {
		out(P "FAIL the message carries no SCM_RIGHTS with two descriptors (msg_flags 0x%x)\n", mh.msg_flags);
		return 1;
	}
	memcpy(fds, CMSG_DATA(c), 2 * sizeof(int));
	return 0;
}

/* --fence: one flip's out-fence, see the comment at the top */
static struct {
	unsigned fences, pending, timeouts, errors;
	unsigned buf_pending, buf_timeouts, buf_errors, export_unsignalled;
	uint64_t min_ns, max_ns;
} fs = { .min_ns = UINT64_MAX };

static int fence_step(int fd, int fence_fd, const struct buf *other, uint64_t t_commit)
{
	struct dma_buf_import_sync_file imp = { .flags = DMA_BUF_SYNC_WRITE, .fd = fence_fd };
	struct dma_buf_export_sync_file ex = { .flags = DMA_BUF_SYNC_READ, .fd = -1 };
	int dfd = -1, r;

	if (fence_fd < 0) {
		out(P "FAIL the commit stored no descriptor through OUT_FENCE_PTR (%d)\n", fence_fd);
		return 1;
	}
	fs.fences++;
	r = fd_readable(fence_fd, FENCE_POLL, 0);
	if (r < 0)
		return fail("poll with no timeout on the out-fence sync_file");
	fs.pending += r == 0;

	if (drmPrimeHandleToFD(fd, other->handle, DRM_CLOEXEC | DRM_RDWR, &dfd))
		return fail("drmPrimeHandleToFD (card node, for the fence import)");
	if (ioctl(dfd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &imp))
		return fail("DMA_BUF_IOCTL_IMPORT_SYNC_FILE (write)");
	r = fd_readable(dfd, FENCE_POLL, 0);
	if (r < 0)
		return fail("poll with no timeout on the dma-buf");
	fs.buf_pending += r == 0;

	r = fd_readable(fence_fd, fence_mode, 1000);
	if (r > 0) {
		uint64_t lat = now_ns() - t_commit;

		if (lat < fs.min_ns)
			fs.min_ns = lat;
		if (lat > fs.max_ns)
			fs.max_ns = lat;
	} else if (r == 0) {
		fs.timeouts++;
	} else {
		if (!fs.errors)
			out(P "wait on the sync_file failed: errno %d (%s)\n", errno, strerror(errno));
		fs.errors++;
	}
	r = fd_readable(dfd, fence_mode, 1000);
	if (r == 0) {
		fs.buf_timeouts++;
	} else if (r < 0) {
		if (!fs.buf_errors)
			out(P "wait on the dma-buf failed: errno %d (%s)\n", errno, strerror(errno));
		fs.buf_errors++;
	}
	if (ioctl(dfd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &ex))
		return fail("DMA_BUF_IOCTL_EXPORT_SYNC_FILE (read)");
	if (fd_readable(ex.fd, FENCE_POLL, 0) != 1)
		fs.export_unsignalled++;
	close(ex.fd);
	close(dfd);
	close(fence_fd);
	return 0;
}

/*
 * Where a flip's time goes, to tell a late completion in the kernel from a late wakeup here:
 *   interval    between two events as this process sees them (after its read() returned)
 *   kernel      between the timestamps the kernel put into the two events
 *   commit      the time the non-blocking commit call took
 *   late        events whose kernel interval was more than 1.5 frames (a missed frame), and
 *   early       less than half a frame, at 60 Hz
 */
struct flip_stats {
	unsigned flips, busy_retries, late, early;
	uint64_t min_iv, max_iv, min_kiv, max_kiv, min_commit, max_commit, sum_commit;
	double elapsed;
};

/* Flips between the two buffers for `seconds`, or for max_flips flips if that is not 0. */
static uint32_t p_in_fence;	/* the plane's IN_FENCE_FD property, with --in-fence */
static unsigned in_fences;

static int flip_loop(int fd, uint32_t plane_id, uint32_t p_fb, uint32_t crtc_id, uint32_t p_out_fence,
		     const struct buf *bufs, double seconds, unsigned max_flips, struct flip_stats *st)
{
	uint64_t start = now_ns(), last = start, end = start + (uint64_t)(seconds * 1e9);
	uint64_t last_event_us = event_us;
	unsigned next = 1;

	memset(st, 0, sizeof(*st));
	st->min_iv = st->min_kiv = st->min_commit = UINT64_MAX;
	while (max_flips ? st->flips < max_flips : now_ns() < end) {
		int tries = 0, ret;
		int32_t out_fence = -1;
		uint64_t t_commit;
		uint64_t t_commit_done = 0;
		int gpu_fence = -1;

		if (p_in_fence) {
			struct dma_buf_export_sync_file ex = { .flags = DMA_BUF_SYNC_READ, .fd = -1 };

			if (ioctl(bufs[next].dfd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &ex))
				return fail("DMA_BUF_IOCTL_EXPORT_SYNC_FILE on the GPU driver's dma-buf");
			gpu_fence = ex.fd;
		}

		/*
		 * EBUSY means the previous commit on the CRTC is not finished although its event
		 * has been read; counted, retried at most 100 times 1 ms apart, and reported.
		 */
		for (;;) {
			drmModeAtomicReq *req = drmModeAtomicAlloc();

			drmModeAtomicAddProperty(req, plane_id, p_fb, bufs[next].fb);
			if (gpu_fence >= 0)
				drmModeAtomicAddProperty(req, plane_id, p_in_fence, (uint64_t)gpu_fence);
			if (p_out_fence)
				drmModeAtomicAddProperty(req, crtc_id, p_out_fence, (uint64_t)(uintptr_t)&out_fence);
			t_commit = now_ns();
			ret = drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK,
						  (void *)(uintptr_t)(st->flips + 1));
			drmModeAtomicFree(req);
			if (!ret) {
				uint64_t took = (t_commit_done = now_ns()) - t_commit;

				if (took < st->min_commit)
					st->min_commit = took;
				if (took > st->max_commit)
					st->max_commit = took;
				st->sum_commit += took;
			}
			if (!ret || errno != EBUSY || ++tries > 100)
				break;
			st->busy_retries++;
			usleep(1000);
		}
		if (ret) {
			out(P "flip %u (after %d EBUSY retries)%s: ", st->flips + 1, tries,
			    gpu_fence >= 0 ? ", IN_FENCE_FD = the GPU driver's sync_file" : "");
			return fail("drmModeAtomicCommit (PAGE_FLIP_EVENT | NONBLOCK)");
		}
		if (gpu_fence >= 0) {
			close(gpu_fence);
			in_fences++;
		}
		if (p_out_fence && fence_step(fd, out_fence, &bufs[next ^ 1], t_commit))
			return 1;
		if (wait_flip(fd, "flip"))
			return 1;
		uint64_t t = now_ns(), iv = t - last;

		uint64_t kiv = event_us - last_event_us;

		if (st->flips > 0) {
			if (iv < st->min_iv)
				st->min_iv = iv;
			if (iv > st->max_iv)
				st->max_iv = iv;
			if (kiv < st->min_kiv)
				st->min_kiv = kiv;
			if (kiv > st->max_kiv)
				st->max_kiv = kiv;
			st->late += kiv > 25000;
			st->early += kiv < 8333;
			/* which flips: at the start of a run, periodic, or anywhere (first 16 shown) */
			if ((kiv > 25000 || kiv < 8333) && st->late + st->early <= 16)
				out(P "timing: flip %u at %.3f s: kernel interval %.3f ms (%s), seen here %.3f ms, "
				    "commit call %.3f ms\n", st->flips + 1, (t - start) / 1e9, kiv / 1e3,
				    kiv > 25000 ? "late" : "early", iv / 1e6, (t_commit_done - t_commit) / 1e6);
		}
		last_event_us = event_us;
		last = t;
		st->flips++;
		next ^= 1;
	}
	st->elapsed = (double)(last - start) / 1e9;
	return 0;
}

int main(int argc, char **argv)
{
	const char *device = NULL;	/* default: the card node whose driver is "apple" */
	char found_path[64] = "";
	double seconds = 2.0;
	uint32_t nonce = 0x5d0c0f11;
	int do_flip = 1;
	const char *prime_from = NULL;
	int scm_rights = 0;
	unsigned dpms_cycles = 0, dpms_off_ms = 2000;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--device") && i + 1 < argc)
			device = argv[++i];
		else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
			seconds = atof(argv[++i]);
		else if (!strcmp(argv[i], "--nonce") && i + 1 < argc)
			nonce = (uint32_t)strtoul(argv[++i], NULL, 16);
		else if (!strcmp(argv[i], "--prime-from") && i + 1 < argc)
			prime_from = argv[++i];
		else if (!strcmp(argv[i], "--wait") && i + 1 < argc) {
			const char *m = argv[++i];

			wait_mode = !strcmp(m, "poll") ? WAIT_POLL : !strcmp(m, "select") ? WAIT_SELECT :
				    !strcmp(m, "kqueue") ? WAIT_KQUEUE : WAIT_READ;
		} else if (!strcmp(argv[i], "--fence") && i + 1 < argc) {
			const char *m = argv[++i];

			fence_mode = !strcmp(m, "select") ? FENCE_SELECT : !strcmp(m, "kqueue") ? FENCE_KQUEUE : FENCE_POLL;
		} else if (!strcmp(argv[i], "--scm-rights"))
			scm_rights = 1;
		else if (!strcmp(argv[i], "--dmabuf-mmap"))
			dmabuf_mmap = 1;
		else if (!strcmp(argv[i], "--in-fence"))
			in_fence = 1;
		else if (!strcmp(argv[i], "--dmabuf-ioctls"))
			dmabuf_ioctls = 1;
		else if (!strcmp(argv[i], "--dpms-cycles") && i + 1 < argc)
			dpms_cycles = (unsigned)atoi(argv[++i]);
		else if (!strcmp(argv[i], "--dpms-off-ms") && i + 1 < argc)
			dpms_off_ms = (unsigned)atoi(argv[++i]);
		else if (!strcmp(argv[i], "--no-flip"))
			do_flip = 0;
	}
	if (dmabuf_mmap && prime_from && !scm_rights) {
		fprintf(stderr, P "--dmabuf-mmap with --prime-from needs --scm-rights\n");
		return 2;
	}
	if (dmabuf_mmap)
		dmabuf_ioctls = 1;
	if (in_fence && !prime_from) {
		fprintf(stderr, P "--in-fence needs --prime-from\n");
		return 2;
	}
	if (scm_rights && !prime_from) {
		fprintf(stderr, P "--scm-rights needs --prime-from\n");
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	console_fd = open("/dev/console", O_WRONLY | O_NOCTTY);
	out(P "start: device %s, %.1f s, nonce %08x\n", device ? device : "(by driver name)", seconds, nonce);

	int fd;
	if (device) {
		fd = open(device, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			return fail(device);
	} else {
		fd = open_by_driver("card", "apple", found_path, sizeof(found_path));
		if (fd < 0) {
			out(P "FAIL no /dev/dri/card* with DRM driver \"apple\" (the display driver)\n");
			return 1;
		}
		device = found_path;
	}
	out(P "display node %s\n", device);
	if (wait_mode == WAIT_KQUEUE) {
		struct kevent ev;

		wait_kq = kqueue();
		EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
		if (wait_kq < 0 || kevent(wait_kq, &ev, 1, NULL, 0, NULL) < 0)
			return fail("kqueue registration of the DRM fd (EVFILT_READ)");
	}
	drmVersion *v = drmGetVersion(fd);
	if (v) {
		out(P "driver %s %d.%d.%d (%s)\n", v->name, v->version_major, v->version_minor,
		       v->version_patchlevel, v->desc);
		drmFreeVersion(v);
	}
	if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1))
		return fail("DRM_CLIENT_CAP_UNIVERSAL_PLANES");
	if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1))
		return fail("DRM_CLIENT_CAP_ATOMIC");

	out(P "step: drmModeGetResources\n");
	drmModeRes *res = drmModeGetResources(fd);
	if (!res)
		return fail("drmModeGetResources");
	out(P "resources: %d crtcs, %d connectors, %d encoders\n", res->count_crtcs, res->count_connectors,
	       res->count_encoders);

	drmModeConnector *conn = NULL;
	for (int i = 0; i < res->count_connectors && !conn; i++) {
		drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);

		if (c)
			out(P "connector %u type %u: %s, %d modes, %ux%u mm\n", c->connector_id,
			       c->connector_type, c->connection == DRM_MODE_CONNECTED ? "connected" : "not connected",
			       c->count_modes, c->mmWidth, c->mmHeight);
		if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0)
			conn = c;
		else
			drmModeFreeConnector(c);
	}
	if (!conn) {
		out(P "FAIL no connected connector with a mode\n");
		return 1;
	}
	drmModeModeInfo mode = conn->modes[0];
	for (int i = 0; i < conn->count_modes; i++)
		if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED)
			mode = conn->modes[i];
	out(P "mode %s %ux%u @ %u Hz (clock %u kHz)\n", mode.name, mode.hdisplay, mode.vdisplay,
	       mode.vrefresh, mode.clock);

	/* the CRTC: the first one an encoder of this connector can drive */
	uint32_t crtc_id = 0;
	int crtc_index = -1;
	for (int e = 0; e < conn->count_encoders && !crtc_id; e++) {
		drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[e]);

		for (int c = 0; enc && c < res->count_crtcs && !crtc_id; c++)
			if (enc->possible_crtcs & (1u << c)) {
				crtc_id = res->crtcs[c];
				crtc_index = c;
			}
		drmModeFreeEncoder(enc);
	}
	if (!crtc_id) {
		out(P "FAIL no CRTC for connector %u\n", conn->connector_id);
		return 1;
	}
	out(P "crtc %u (index %d)\n", crtc_id, crtc_index);

	out(P "step: drmModeGetPlaneResources\n");
	drmModePlaneRes *planes = drmModeGetPlaneResources(fd);
	if (!planes)
		return fail("drmModeGetPlaneResources");
	uint32_t plane_id = 0;
	for (uint32_t i = 0; i < planes->count_planes; i++) {
		drmModePlane *pl = drmModeGetPlane(fd, planes->planes[i]);
		uint64_t type = 99;

		if (!pl)
			continue;
		prop_id(fd, pl->plane_id, DRM_MODE_OBJECT_PLANE, "type", &type);
		out(P "plane %u type %" PRIu64 " possible_crtcs 0x%x, %u formats, first %.4s\n", pl->plane_id, type,
		       pl->possible_crtcs, pl->count_formats, pl->count_formats ? (const char *)&pl->formats[0] : "none");
		if (!plane_id && type == DRM_PLANE_TYPE_PRIMARY && (pl->possible_crtcs & (1u << crtc_index)))
			plane_id = pl->plane_id;
		drmModeFreePlane(pl);
	}
	if (!plane_id) {
		out(P "FAIL no primary plane for crtc %u\n", crtc_id);
		return 1;
	}
	out(P "using plane %u\n", plane_id);

	uint32_t p_conn_crtc = prop_id(fd, conn->connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", NULL);
	uint32_t p_mode = prop_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID", NULL);
	uint32_t p_active = prop_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE", NULL);
	static const char *const pn[] = { "FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H",
					  "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H" };
	uint32_t pp[10];
	int missing = !p_conn_crtc || !p_mode || !p_active;
	for (int i = 0; i < 10; i++) {
		pp[i] = prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, pn[i], NULL);
		missing |= !pp[i];
	}
	if (missing) {
		out(P "FAIL a required property is missing\n");
		return 1;
	}

	uint32_t p_out_fence = 0;
	if (fence_mode != FENCE_NONE) {
		p_out_fence = prop_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "OUT_FENCE_PTR", NULL);
		if (!p_out_fence) {
			out(P "FAIL the CRTC has no OUT_FENCE_PTR property\n");
			return 1;
		}
	}

	struct buf bufs[2] = { { 0 }, { 0 } };
	int render_fd = -1, scm_sock = -1;
	pid_t scm_pid = -1;
	if (scm_rights) {
		static const uint32_t colours[2] = { 0x00203080, 0x00804020 };
		struct scm_msg m;
		int sv[2], got[2];

		if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv))
			return fail("socketpair");
		out(P "step: fork; the child exports, the parent (pid %d) imports\n", (int)getpid());
		fflush(stdout);
		scm_pid = fork();
		if (scm_pid < 0)
			return fail("fork");
		if (scm_pid == 0) {
			close(sv[0]);
			close(fd);
			scm_child(sv[1], mode.hdisplay, mode.vdisplay, nonce);
		}
		close(sv[1]);
		scm_sock = sv[0];
		if (scm_receive(scm_sock, got, &m))
			return 1;
		out(P "received descriptors %d and %d over SCM_RIGHTS from pid %d\n", got[0], got[1], (int)scm_pid);
		for (int k = 0; k < 2; k++) {
			bufs[k].pitch = m.pitch[k];
			bufs[k].size = m.size[k];
			if (dmabuf_ioctls && dmabuf_ioctl_check(got[k], "in the process that received it"))
				return 1;
			if (dmabuf_mmap && dmabuf_map_and_write(got[k], k, mode.hdisplay, mode.vdisplay, m.pitch[k],
								m.size[k], colours[k]))
				return 1;
			out(P "step: PRIME import of the received descriptor %d into the card node\n", got[k]);
			if (drmPrimeFDToHandle(fd, got[k], &bufs[k].handle))
				return fail("drmPrimeFDToHandle (card node, descriptor received over SCM_RIGHTS)");
			if (in_fence)
				bufs[k].dfd = got[k];
			else
				close(got[k]);
			if (add_fb(fd, &bufs[k], mode.hdisplay, mode.vdisplay, colours[k]))
				return 1;
		}
	} else {
	if (prime_from) {
		char rpath[64] = "";

		if (!strcmp(prime_from, "auto")) {
			render_fd = open_by_driver("renderD", "asahi", rpath, sizeof(rpath));
			if (render_fd < 0) {
				out(P "FAIL no /dev/dri/renderD* with DRM driver \"asahi\" (the GPU driver)\n");
				return 1;
			}
			prime_from = rpath;
		} else {
			render_fd = open(prime_from, O_RDWR | O_CLOEXEC);
			if (render_fd < 0)
				return fail(prime_from);
		}
		out(P "buffers come from the GPU driver on %s\n", prime_from);
	}
	if (make_buf(fd, render_fd, &bufs[0], mode.hdisplay, mode.vdisplay, 0x00203080, nonce) ||
	    make_buf(fd, render_fd, &bufs[1], mode.hdisplay, mode.vdisplay, 0x00804020, ~nonce))
		return 1;
	for (int k = 0; dmabuf_mmap && k < 2; k++) {
		int dfd = -1;
		uint32_t bad;

		out(P "step: PRIME export of dumb buffer %d from the card node\n", k);
		if (drmPrimeHandleToFD(fd, bufs[k].handle, DRM_CLOEXEC | DRM_RDWR, &dfd))
			return fail("drmPrimeHandleToFD (card node, for the dma-buf mapping)");
		if (dmabuf_map_and_write(dfd, k, mode.hdisplay, mode.vdisplay, bufs[k].pitch, bufs[k].size,
					 k ? 0x00804020 : 0x00203080))
			return 1;
		close(dfd);
		bad = pattern_bad(&bufs[k], k, mode.hdisplay);
		out(P "pattern of buffer %d read back through the dumb buffer's mapping: %u pixels differ%s\n", k, bad,
		    bad ? " FAILED" : "");
		if (bad)
			return 1;
	}
	}

	uint32_t mode_blob = 0;
	if (drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &mode_blob))
		return fail("drmModeCreatePropertyBlob (mode)");

	drmModeAtomicReq *req = drmModeAtomicAlloc();
	uint64_t pv[10] = { bufs[0].fb, crtc_id, 0, 0, (uint64_t)mode.hdisplay << 16, (uint64_t)mode.vdisplay << 16,
			    0, 0, mode.hdisplay, mode.vdisplay };
	drmModeAtomicAddProperty(req, conn->connector_id, p_conn_crtc, crtc_id);
	drmModeAtomicAddProperty(req, crtc_id, p_mode, mode_blob);
	drmModeAtomicAddProperty(req, crtc_id, p_active, 1);
	for (int i = 0; i < 10; i++)
		drmModeAtomicAddProperty(req, plane_id, pp[i], pv[i]);
	out(P "step: atomic commit with ALLOW_MODESET\n");
	uint64_t t_commit = now_ns();
	if (drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_PAGE_FLIP_EVENT, NULL))
		return fail("drmModeAtomicCommit (ALLOW_MODESET | PAGE_FLIP_EVENT, first frame)");
	drmModeAtomicFree(req);
	uint64_t t_returned = now_ns();
	out(P "first frame committed: fb %u, nonce %08x; the blocking commit took %.3f ms\n", bufs[0].fb, nonce,
	    (t_returned - t_commit) / 1e6);
	/* a client must not flip again before the previous commit's event: wait for it */
	if (wait_flip(fd, "first frame"))
		return 1;
	out(P "first frame's flip-complete event arrived %.3f ms after the commit returned\n",
	    (now_ns() - t_returned) / 1e6);

	if (!do_flip) {
		sleep((unsigned)seconds);
		out(P "PASS (no flips requested)\n");
		return 0;
	}

	if (in_fence) {
		p_in_fence = prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "IN_FENCE_FD", NULL);
		if (!p_in_fence) {
			out(P "FAIL the plane has no IN_FENCE_FD property\n");
			return 1;
		}
	}
	out(P "step: flipping\n");
	struct flip_stats st;

	if (flip_loop(fd, plane_id, pp[0], crtc_id, p_out_fence, bufs, seconds, 0, &st))
		return 1;

	/*
	 * The buffers, read back through the mapping they were written through (for
	 * --prime-from: the render node's; for --scm-rights: in the exporting process), must
	 * still hold what was written: the display only reads them.
	 */
	if (scm_rights) {
		uint32_t bad[4] = { ~0u, ~0u, ~0u, ~0u };
		int status = 0;

		out(P "step: readback in the exporting process\n");
		if (write(scm_sock, "r", 1) != 1 || read(scm_sock, bad, sizeof(bad)) != (ssize_t)sizeof(bad))
			return fail("readback request to the exporting process");
		for (int k = 0; k < 2; k++)
			out(P "readback of buffer %d in pid %d: %u pixels differ%s\n", k, (int)scm_pid, bad[k],
			    bad[k] ? " FAILED" : "");
		for (int k = 0; dmabuf_mmap && k < 2; k++)
			out(P "pattern of buffer %d read back through the render node's mapping in pid %d: %u pixels differ%s\n",
			    k, (int)scm_pid, bad[2 + k], bad[2 + k] ? " FAILED" : "");
		if (dmabuf_mmap && (bad[2] || bad[3]))
			return 1;
		if (waitpid(scm_pid, &status, 0) != scm_pid)
			return fail("waitpid for the exporting process");
		out(P "exporting process exited: status 0x%x%s\n", status,
		    WIFEXITED(status) && WEXITSTATUS(status) == 0 ? "" : " FAILED");
		if (bad[0] || bad[1] || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
			return 1;
	} else {
		for (int k = 0; k < 2; k++) {
			uint32_t want = k == 0 ? 0x00203080 : 0x00804020;
			uint32_t bad = count_bad(&bufs[k], mode.hdisplay, mode.vdisplay, want);

			out(P "readback of buffer %d below the nonce rows: %u pixels differ from 0x%06x%s\n", k, bad, want,
			    bad ? " FAILED" : "");
			if (bad)
				return 1;
		}
	}

	out(P "%u flips in %.3f s: %.2f per second, interval min %.3f ms max %.3f ms, %u EBUSY retries\n", st.flips,
	    st.elapsed, st.elapsed > 0 ? st.flips / st.elapsed : 0.0, st.flips > 1 ? st.min_iv / 1e6 : 0.0,
	    st.flips > 1 ? st.max_iv / 1e6 : 0.0, st.busy_retries);
	if (st.flips > 1)
		out(P "timing: kernel event timestamps min %.3f ms max %.3f ms apart, %u late (> 25 ms), %u early "
		    "(< 8.3 ms); commit call min %.3f ms mean %.3f ms max %.3f ms\n", st.min_kiv / 1e3, st.max_kiv / 1e3,
		    st.late, st.early, st.min_commit / 1e6, st.sum_commit / 1e6 / st.flips, st.max_commit / 1e6);
	{
		static const char *const wn[] = { "read", "poll", "select", "kqueue" };

		out(P "wait mode %s: %u timeouts, %u errors%s\n", wn[wait_mode], wait_timeouts, wait_errors,
		    wait_timeouts || wait_errors ? " FAILED" : "");
		if (wait_timeouts || wait_errors)
			return 1;
	}
	if (fence_mode != FENCE_NONE) {
		static const char *const fn[] = { "none", "poll", "select", "kqueue" };
		int bad = fs.timeouts || fs.errors || fs.buf_timeouts || fs.buf_errors || fs.export_unsignalled ||
			  !fs.pending || !fs.buf_pending;

		out(P "fence wait %s: %u out-fences, %u pending after the commit, %u timeouts, %u errors, "
		    "commit to wakeup min %.3f ms max %.3f ms\n", fn[fence_mode], fs.fences, fs.pending, fs.timeouts,
		    fs.errors, fs.fences > fs.timeouts + fs.errors ? fs.min_ns / 1e6 : 0.0, fs.max_ns / 1e6);
		out(P "fence in the dma-buf: %u pending after the import, %u timeouts, %u errors, "
		    "%u exported sync_files not signalled%s\n", fs.buf_pending, fs.buf_timeouts, fs.buf_errors,
		    fs.export_unsignalled, bad ? " FAILED" : "");
		if (!fs.pending || !fs.buf_pending)
			out(P "no fence was seen pending, so no wakeup was tested FAILED\n");
		if (bad)
			return 1;
	}
	if (in_fence)
		out(P "in-fence: %u commits took a sync_file of the GPU driver as IN_FENCE_FD%s\n", in_fences,
		    in_fences ? "" : " FAILED");
	if (in_fence && !in_fences)
		return 1;
	if (st.flips < 2) {
		out(P "FAIL fewer than two flips completed\n");
		return 1;
	}
	if (dpms_cycles) {
		unsigned ok = 0;
		double worst_rate = 1e9, worst_on_ms = 0, worst_off_ms = 0, worst_first_ms = 0;

		out(P "step: %u display sleep cycles, %u ms off each\n", dpms_cycles, dpms_off_ms);
		for (unsigned c = 1; c <= dpms_cycles; c++) {
			struct flip_stats ds;
			uint64_t t0, t1, t2, t3;

			req = drmModeAtomicAlloc();
			drmModeAtomicAddProperty(req, crtc_id, p_active, 0);
			t0 = now_ns();
			if (drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL)) {
				out(P "sleep cycle %u: ", c);
				return fail("drmModeAtomicCommit (ACTIVE = 0, ALLOW_MODESET)");
			}
			drmModeAtomicFree(req);
			t1 = now_ns();
			usleep(dpms_off_ms * 1000);
			req = drmModeAtomicAlloc();
			drmModeAtomicAddProperty(req, crtc_id, p_active, 1);
			t2 = now_ns();
			if (drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_PAGE_FLIP_EVENT, NULL)) {
				out(P "sleep cycle %u: ", c);
				return fail("drmModeAtomicCommit (ACTIVE = 1, ALLOW_MODESET | PAGE_FLIP_EVENT)");
			}
			drmModeAtomicFree(req);
			t3 = now_ns();
			if (wait_flip(fd, "wake"))
				return 1;
			double first_ms = (now_ns() - t3) / 1e6, off_ms = (t1 - t0) / 1e6, on_ms = (t3 - t2) / 1e6;

			if (flip_loop(fd, plane_id, pp[0], crtc_id, 0, bufs, 0.5, 0, &ds))
				return 1;
			double rate = ds.elapsed > 0 ? ds.flips / ds.elapsed : 0.0;

			if (c <= 3 || c == dpms_cycles || rate < 50.0)
				out(P "sleep cycle %u: off commit %.1f ms, on commit %.1f ms, first flip event %.1f ms later, "
				    "then %u flips at %.2f per second%s\n", c, off_ms, on_ms, first_ms, ds.flips, rate,
				    rate < 50.0 ? " FAILED" : "");
			if (rate < 50.0)
				return 1;
			ok++;
			if (rate < worst_rate)
				worst_rate = rate;
			if (on_ms > worst_on_ms)
				worst_on_ms = on_ms;
			if (off_ms > worst_off_ms)
				worst_off_ms = off_ms;
			if (first_ms > worst_first_ms)
				worst_first_ms = first_ms;
		}
		out(P "display sleep: %u of %u cycles woke and flipped; slowest off commit %.1f ms, on commit %.1f ms, "
		    "first flip event %.1f ms, lowest rate %.2f per second\n", ok, dpms_cycles, worst_off_ms,
		    worst_on_ms, worst_first_ms, worst_rate);
	}
	if (scm_rights) {
		struct flip_stats st2;

		out(P "step: ten flips after the exporting process exited\n");
		if (flip_loop(fd, plane_id, pp[0], crtc_id, 0, bufs, 0, 10, &st2))
			return 1;
		out(P "%u flips after the exporting process exited\n", st2.flips);
	}
	out(P "PASS\n");
	return 0;
}
