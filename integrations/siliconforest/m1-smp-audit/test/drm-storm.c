// SPDX-License-Identifier: MIT
// drm-storm: concurrency stress for the DRM device nodes and the Linux-style fds (dma-buf,
// sync file) of the SiliconForest M1 kernel, written against the findings of
// docs/roadmap/research/smp-lock-audit.md.  Every case runs --threads threads (default 8) for
// --seconds each; a case passes when no call returned an error it should not and every data
// check held.  A kernel panic or hang is the other way a case fails; each case announces itself
// on /dev/console first so the console names the case that was running.
//
// Cases (--cases a,b,... ; default all):
//   open      open/close the render node from all threads (slot table, clone minors)
//   ioctl     GET_PARAMS/VERSION/GET_CAP on one shared fd from all threads
//   gem       per-thread create, mmap, pattern write+verify, munmap, close on one shared fd
//   syncobj   create/wait(0)/destroy on one shared fd; half the threads wait on handles the
//             other half create and destroy (wrong-handle errors are expected and counted)
//   prime     export a GEM object, import the fd into a second fd, close both handles and the
//             fd, in every order, from all threads
//   syncfile  syncobj -> sync file fd (signalled stub fence), poll it, merge-free path:
//             import into the dma-buf (IMPORT/EXPORT_SYNC_FILE), poll the dma-buf, close
//   closerace one thread closes and reopens a node fd number while the others issue ioctls on
//             it (they must get EBADF/ENXIO/ENOTTY or success, never crash)
//   mmaprace  threads mmap and munmap the same GEM object while another closes the handle
//
// Output: one "DRMSTORM case=... threads=.. seconds=.. ops=.. errors=.. unexpected=.. result=ok|FAIL"
// line per case and "DRMSTORM_DONE status=ok|failed".  Exit status 0 only for ok.
//
// usage: drm-storm [--threads N] [--seconds S] [--cases list] [--node /dev/dri/renderDN]

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <xf86drm.h>

#define P "drm-storm: "
#define MAXT 32

struct gem_create { uint64_t size; uint32_t flags, vm_id, handle, pad; };
struct gem_mmap { uint32_t handle, flags; uint64_t offset; };
struct get_params { uint32_t group, pad; uint64_t pointer, size; };
struct dmabuf_sync_file { uint32_t flags; int32_t fd; };
#define IOCTL_GEM_CREATE DRM_IOWR(DRM_COMMAND_BASE + 5, struct gem_create)
#define IOCTL_GEM_MMAP   DRM_IOWR(DRM_COMMAND_BASE + 6, struct gem_mmap)
#define IOCTL_GET_PARAMS DRM_IOW(DRM_COMMAND_BASE + 0, struct get_params)
#define DMA_BUF_EXPORT_SYNC_FILE _IOWR('b', 2, struct dmabuf_sync_file)
#define DMA_BUF_IMPORT_SYNC_FILE _IOW('b', 3, struct dmabuf_sync_file)
#define OBJ_SIZE (4u * 16384u)

static char node_path[128];
static int nthreads = 8, console_fd = -1;
static double seconds = 5.0;
static atomic_ullong ops, errors, unexpected;
static atomic_int stop;
static int shared_fd = -1, second_fd = -1;
static atomic_uint shared_handles[64];
static atomic_int race_fd;

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void say(const char *fmt, ...)
{
	char line[400];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (n > (int)sizeof(line) - 1)
		n = sizeof(line) - 1;
	fflush(stdout);
	(void)!write(1, line, (size_t)n);
	if (console_fd >= 0)
		(void)!write(console_fd, line, (size_t)n);
}

// an error nobody should see; the first few are printed with the call
static void bad(const char *what)
{
	int e = errno;
	if (atomic_fetch_add(&unexpected, 1) < 8)
		say(P "UNEXPECTED %s: errno %d (%s)\n", what, e, strerror(e));
}

static int find_node(void)
{
	DIR *d = opendir("/dev/dri");
	struct dirent *e;
	int found = 0;
	if (!d)
		return 0;
	while (!found && (e = readdir(d)) != NULL) {
		if (strncmp(e->d_name, "renderD", 7))
			continue;
		snprintf(node_path, sizeof(node_path), "/dev/dri/%s", e->d_name);
		int fd = open(node_path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		drmVersion *v = drmGetVersion(fd);
		found = v && !strcmp(v->name, "asahi");
		drmFreeVersion(v);
		close(fd);
	}
	closedir(d);
	return found;
}

static int gem_new(int fd, uint32_t *handle)
{
	struct gem_create c = { .size = OBJ_SIZE };
	if (drmIoctl(fd, IOCTL_GEM_CREATE, &c))
		return -1;
	*handle = c.handle;
	return 0;
}

static void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close cl = { .handle = handle };
	if (drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &cl))
		bad("DRM_IOCTL_GEM_CLOSE");
}

static void *case_open(void *arg)
{
	(void)arg;
	while (!stop) {
		int fd = open(node_path, O_RDWR | O_CLOEXEC);
		if (fd < 0) {
			bad("open of the render node");
			continue;
		}
		drmVersion *v = drmGetVersion(fd);
		if (!v || strcmp(v->name, "asahi"))
			bad("drmGetVersion on a fresh open");
		drmFreeVersion(v);
		close(fd);
		ops++;
	}
	return NULL;
}

static void *case_ioctl(void *arg)
{
	(void)arg;
	uint8_t params[616];
	while (!stop) {
		struct get_params gp = { .pointer = (uintptr_t)params, .size = sizeof(params) };
		uint64_t cap = 0;
		memset(params, 0, sizeof(params));
		if (drmIoctl(shared_fd, IOCTL_GET_PARAMS, &gp))
			bad("DRM_IOCTL_ASAHI_GET_PARAMS");
		else if (((uint32_t *)params)[2] != 13)	// gpu_generation on this machine
			bad("GET_PARAMS returned a different gpu_generation");
		if (drmGetCap(shared_fd, DRM_CAP_PRIME, &cap))
			bad("DRM_IOCTL_GET_CAP");
		drmVersion *v = drmGetVersion(shared_fd);
		if (!v)
			bad("DRM_IOCTL_VERSION");
		drmFreeVersion(v);
		ops += 3;
	}
	return NULL;
}

static void *case_gem(void *arg)
{
	uint32_t tag = 0x51000000u | (uint32_t)(uintptr_t)arg << 16;
	while (!stop) {
		uint32_t handle;
		struct gem_mmap m = { 0 };
		if (gem_new(shared_fd, &handle)) {
			bad("DRM_IOCTL_ASAHI_GEM_CREATE");
			continue;
		}
		m.handle = handle;
		if (drmIoctl(shared_fd, IOCTL_GEM_MMAP, &m)) {
			bad("DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET");
		} else {
			uint32_t *p = mmap(NULL, OBJ_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shared_fd, (off_t)m.offset);
			if (p == MAP_FAILED) {
				bad("mmap of a GEM object");
			} else {
				// a fresh object must be zero, and must hold what this thread writes
				for (unsigned i = 0; i < OBJ_SIZE / 4; i += 1024)
					if (p[i]) {
						errno = 0;
						bad("fresh GEM object is not zero");
						break;
					}
				for (unsigned i = 0; i < OBJ_SIZE / 4; i += 64)
					p[i] = tag | (i & 0xffff);
				for (unsigned i = 0; i < OBJ_SIZE / 4; i += 64)
					if (p[i] != (tag | (i & 0xffff))) {
						errno = 0;
						bad("GEM object lost a write (another thread's pages?)");
						break;
					}
				munmap(p, OBJ_SIZE);
			}
		}
		gem_close(shared_fd, handle);
		ops++;
	}
	return NULL;
}

static void *case_syncobj(void *arg)
{
	unsigned id = (unsigned)(uintptr_t)arg;
	while (!stop) {
		if (id & 1) {	// creator/destroyer, publishes its handle for the waiters
			uint32_t h;
			if (drmSyncobjCreate(shared_fd, DRM_SYNCOBJ_CREATE_SIGNALED, &h)) {
				bad("DRM_IOCTL_SYNCOBJ_CREATE");
				continue;
			}
			atomic_store(&shared_handles[id], h);
			if (drmSyncobjWait(shared_fd, &h, 1, 0, 0, NULL))
				bad("DRM_IOCTL_SYNCOBJ_WAIT on a signalled syncobj");
			atomic_store(&shared_handles[id], 0);
			if (drmSyncobjDestroy(shared_fd, h))
				bad("DRM_IOCTL_SYNCOBJ_DESTROY");
		} else {	// waits on whatever a neighbour published: may be gone already
			uint32_t h = atomic_load(&shared_handles[(id + 1) % (unsigned)nthreads]);
			if (h && drmSyncobjWait(shared_fd, &h, 1, 0, 0, NULL)) {
				if (errno == ENOENT || errno == EINVAL || errno == ETIME)
					errors++;	// destroyed under us: the expected error
				else
					bad("DRM_IOCTL_SYNCOBJ_WAIT on a neighbour's handle");
			}
		}
		ops++;
	}
	return NULL;
}

static void *case_prime(void *arg)
{
	unsigned id = (unsigned)(uintptr_t)arg, round = 0;
	while (!stop) {
		uint32_t h, h2 = 0;
		int fd = -1;
		if (gem_new(shared_fd, &h)) {
			bad("DRM_IOCTL_ASAHI_GEM_CREATE");
			continue;
		}
		if (drmPrimeHandleToFD(shared_fd, h, DRM_CLOEXEC | DRM_RDWR, &fd)) {
			bad("DRM_IOCTL_PRIME_HANDLE_TO_FD");
			gem_close(shared_fd, h);
			continue;
		}
		if (drmPrimeFDToHandle(second_fd, fd, &h2))
			bad("DRM_IOCTL_PRIME_FD_TO_HANDLE into a second open file");
		// every order of dropping the three references
		switch ((round++ + id) % 3) {
		case 0: close(fd); gem_close(shared_fd, h); if (h2) gem_close(second_fd, h2); break;
		case 1: gem_close(shared_fd, h); if (h2) gem_close(second_fd, h2); close(fd); break;
		default: if (h2) gem_close(second_fd, h2); close(fd); gem_close(shared_fd, h); break;
		}
		ops++;
	}
	return NULL;
}

static void *case_syncfile(void *arg)
{
	(void)arg;
	while (!stop) {
		uint32_t so, h;
		int sf = -1, db = -1;
		if (drmSyncobjCreate(shared_fd, DRM_SYNCOBJ_CREATE_SIGNALED, &so)) {
			bad("DRM_IOCTL_SYNCOBJ_CREATE");
			continue;
		}
		if (drmSyncobjExportSyncFile(shared_fd, so, &sf) || sf < 0) {
			bad("DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD (sync file)");
		} else {
			struct pollfd pfd = { .fd = sf, .events = POLLIN };
			int r = poll(&pfd, 1, 1000);
			if (r != 1 || !(pfd.revents & POLLIN)) {
				errno = r < 0 ? errno : 0;
				bad("poll of a signalled sync file did not report POLLIN within 1 s");
			}
		}
		if (!gem_new(shared_fd, &h)) {
			if (drmPrimeHandleToFD(shared_fd, h, DRM_CLOEXEC | DRM_RDWR, &db)) {
				bad("DRM_IOCTL_PRIME_HANDLE_TO_FD");
			} else {
				struct dmabuf_sync_file imp = { .flags = 2 /* write */, .fd = sf }, exp = { .flags = 3, .fd = -1 };
				struct pollfd pfd = { .fd = db, .events = POLLIN | POLLOUT };
				if (sf >= 0 && ioctl(db, DMA_BUF_IMPORT_SYNC_FILE, &imp))
					bad("DMA_BUF_IOCTL_IMPORT_SYNC_FILE");
				if (ioctl(db, DMA_BUF_EXPORT_SYNC_FILE, &exp))
					bad("DMA_BUF_IOCTL_EXPORT_SYNC_FILE");
				else if (exp.fd >= 0)
					close(exp.fd);
				if (poll(&pfd, 1, 1000) != 1)
					bad("poll of an idle dma-buf did not return within 1 s");
				close(db);
			}
			gem_close(shared_fd, h);
		}
		if (sf >= 0)
			close(sf);
		drmSyncobjDestroy(shared_fd, so);
		ops++;
	}
	return NULL;
}

// thread 0 replaces the fd under the others
static void *case_closerace(void *arg)
{
	unsigned id = (unsigned)(uintptr_t)arg;
	uint8_t params[616];
	while (!stop) {
		if (id == 0) {
			int fd = open(node_path, O_RDWR | O_CLOEXEC), old = atomic_exchange(&race_fd, fd);
			if (fd < 0)
				bad("open of the render node");
			if (old >= 0)
				close(old);
			usleep(200);
		} else {
			int fd = atomic_load(&race_fd);
			struct get_params gp = { .pointer = (uintptr_t)params, .size = sizeof(params) };
			uint32_t h;
			if (fd < 0)
				continue;
			if (ioctl(fd, IOCTL_GET_PARAMS, &gp)) {
				if (errno == EBADF || errno == ENXIO || errno == ENOTTY || errno == ENODEV || errno == EINTR)
					errors++;
				else
					bad("GET_PARAMS on an fd being closed");
			}
			// objects made on a file that is closed under us die with the file
			if (!gem_new(fd, &h)) {
				struct drm_gem_close cl = { .handle = h };
				(void)ioctl(fd, DRM_IOCTL_GEM_CLOSE, &cl);
			}
		}
		ops++;
	}
	return NULL;
}

static void *case_mmaprace(void *arg)
{
	unsigned id = (unsigned)(uintptr_t)arg;
	while (!stop) {
		if (id == 0) {	// owner: create, publish, let the others map, close
			uint32_t h;
			struct gem_mmap m = { 0 };
			if (gem_new(shared_fd, &h)) {
				bad("DRM_IOCTL_ASAHI_GEM_CREATE");
				continue;
			}
			m.handle = h;
			if (drmIoctl(shared_fd, IOCTL_GEM_MMAP, &m))
				bad("DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET");
			atomic_store(&shared_handles[1], (unsigned)(m.offset >> 14));	// offsets are 16 KiB multiples
			usleep(300);
			atomic_store(&shared_handles[1], 0);
			gem_close(shared_fd, h);
		} else {
			uint64_t off = (uint64_t)atomic_load(&shared_handles[1]) << 14;
			if (!off)
				continue;
			volatile uint32_t *p = mmap(NULL, OBJ_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shared_fd, (off_t)off);
			if (p == MAP_FAILED) {
				errors++;	// the object went away first: expected
			} else {
				p[0] = id;	// the mapping must stay usable until unmapped, even after the handle is closed
				p[OBJ_SIZE / 4 - 1] = id;
				munmap((void *)p, OBJ_SIZE);
			}
		}
		ops++;
	}
	return NULL;
}

static const struct { const char *name; void *(*fn)(void *); } cases[] = {
	{ "open", case_open }, { "ioctl", case_ioctl }, { "gem", case_gem }, { "syncobj", case_syncobj },
	{ "prime", case_prime }, { "syncfile", case_syncfile }, { "closerace", case_closerace }, { "mmaprace", case_mmaprace },
};

int main(int argc, char **argv)
{
	const char *want = "open,ioctl,gem,syncobj,prime,syncfile,closerace,mmaprace";
	int failed = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--threads") && i + 1 < argc) nthreads = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
		else if (!strcmp(argv[i], "--cases") && i + 1 < argc) want = argv[++i];
		else if (!strcmp(argv[i], "--node") && i + 1 < argc) snprintf(node_path, sizeof(node_path), "%s", argv[++i]);
		else {
			printf("usage: %s [--threads N] [--seconds S] [--cases list] [--node path]\n", argv[0]);
			return 2;
		}
	}
	if (nthreads < 2 || nthreads > MAXT)
		nthreads = 8;
	console_fd = open("/dev/console", O_WRONLY | O_NOCTTY);
	if (!node_path[0] && !find_node()) {
		say("DRMSTORM_DONE status=failed reason=no-asahi-render-node\n");
		return 2;
	}
	say(P "node %s, %d threads, %.1f s per case\n", node_path, nthreads, seconds);

	for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
		pthread_t th[MAXT];
		const char *hit = strstr(want, cases[c].name);
		size_t len = strlen(cases[c].name);
		if (!hit || (hit != want && hit[-1] != ',') || (hit[len] && hit[len] != ','))
			continue;
		shared_fd = open(node_path, O_RDWR | O_CLOEXEC);
		second_fd = open(node_path, O_RDWR | O_CLOEXEC);
		if (shared_fd < 0 || second_fd < 0) {
			say("DRMSTORM case=%s result=FAIL reason=open errno=%d\n", cases[c].name, errno);
			failed = 1;
			break;
		}
		ops = errors = unexpected = 0;
		stop = 0;
		race_fd = -1;
		for (int i = 0; i < 64; i++)
			shared_handles[i] = 0;
		say(P "case %s starts\n", cases[c].name);
		uint64_t t0 = now_ms();
		for (int i = 0; i < nthreads; i++)
			pthread_create(&th[i], NULL, cases[c].fn, (void *)(uintptr_t)i);
		while (now_ms() - t0 < (uint64_t)(seconds * 1000))
			usleep(20000);
		stop = 1;
		for (int i = 0; i < nthreads; i++)
			pthread_join(th[i], NULL);
		if (race_fd >= 0)
			close(race_fd);
		close(shared_fd);
		close(second_fd);
		// the node must still answer after the case
		int fd = open(node_path, O_RDWR | O_CLOEXEC);
		drmVersion *v = fd >= 0 ? drmGetVersion(fd) : NULL;
		int alive = v && !strcmp(v->name, "asahi");
		drmFreeVersion(v);
		if (fd >= 0)
			close(fd);
		int ok = !unexpected && alive && ops > 0;
		say("DRMSTORM case=%s threads=%d seconds=%.1f ops=%llu errors=%llu unexpected=%llu node_alive=%d result=%s\n", cases[c].name,
		    nthreads, (now_ms() - t0) / 1000.0, (unsigned long long)ops, (unsigned long long)errors,
		    (unsigned long long)unexpected, alive, ok ? "ok" : "FAIL");
		failed |= !ok;
	}
	say("DRMSTORM_DONE status=%s\n", failed ? "failed" : "ok");
	return failed;
}
