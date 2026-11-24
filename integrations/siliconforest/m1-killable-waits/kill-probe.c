/* SPDX-License-Identifier: MIT */
/*
 * Native gate for killable GPU clients. Each case forks a child that opens the render
 * node, creates an empty DRM syncobj and waits for a fence to be SUBMITTED to it with an
 * infinite timeout (DRM_IOCTL_SYNCOBJ_WAIT / TIMELINE_WAIT, WAIT_FOR_SUBMIT): no GPU job
 * and no Mesa, exactly the wait loop of drm_syncobj_array_wait_timeout(). The parent then
 * signals it and requires it to be reaped (or, for a caught signal, the ioctl to return
 * EINTR) within 2 s.
 *
 * RUN ONLY ON THE KILLABLE KERNEL (its kern.kernelcacheuuid is the first argument). On a
 * kernel without the fix the child cannot be reaped: it spins in the kernel until reboot.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

struct drm_syncobj_create { uint32_t handle, flags; };
struct drm_syncobj_wait {
	uint64_t handles; int64_t timeout_nsec; uint32_t count_handles, flags, first_signaled, pad;
	uint64_t deadline_nsec;
};
struct drm_syncobj_timeline_wait {
	uint64_t handles, points; int64_t timeout_nsec; uint32_t count_handles, flags, first_signaled, pad;
	uint64_t deadline_nsec;
};
#define DRM_IOCTL_SYNCOBJ_CREATE        _IOWR('d', 0xBF, struct drm_syncobj_create)
#define DRM_IOCTL_SYNCOBJ_WAIT          _IOWR('d', 0xC3, struct drm_syncobj_wait)
#define DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT _IOWR('d', 0xCA, struct drm_syncobj_timeline_wait)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT (1u << 1)

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static volatile sig_atomic_t caught;
static void on_usr1(int s) { (void)s; caught = 1; }

/* child: 0 = the wait returned EINTR after a caught SIGUSR1; never returns otherwise */
static int child(const char *node, int timeline, int ready_fd, int catch_usr1)
{
	struct drm_syncobj_create c = { 0, 0 };
	int fd = open(node, O_RDWR | O_CLOEXEC), r;
	char ok = 'R';

	if (catch_usr1) {
		struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_usr1;
		sigaction(SIGUSR1, &sa, NULL);   /* no SA_RESTART: EINTR must reach us */
	}
	if (fd < 0 || ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &c) != 0) { ok = 'E'; (void)!write(ready_fd, &ok, 1); return 3; }
	(void)!write(ready_fd, &ok, 1);
	if (timeline) {
		uint64_t point = 1;
		struct drm_syncobj_timeline_wait w = { (uintptr_t)&c.handle, (uintptr_t)&point, INT64_MAX, 1,
		    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, 0, 0, 0 };
		r = ioctl(fd, DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT, &w);
	} else {
		struct drm_syncobj_wait w = { (uintptr_t)&c.handle, INT64_MAX, 1, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, 0, 0, 0 };
		r = ioctl(fd, DRM_IOCTL_SYNCOBJ_WAIT, &w);
	}
	if (r != 0 && errno == EINTR && caught)
		return 0;
	fprintf(stderr, "child: wait returned %d errno %d caught %d\n", r, errno, (int)caught);
	return 4;
}

static int one(const char *node, int timeline, int sig)
{
	int p[2]; char ready = 0; pid_t pid; int status = 0; double t0, t;
	const char *name = sig == SIGKILL ? "SIGKILL" : sig == SIGTERM ? "SIGTERM" : "SIGUSR1-caught";

	if (pipe(p)) { perror("pipe"); return 1; }
	pid = fork();
	if (pid < 0) { perror("fork"); return 1; }
	if (pid == 0) { close(p[0]); _exit(child(node, timeline, p[1], sig == SIGUSR1)); }
	close(p[1]);
	if (read(p[0], &ready, 1) != 1 || ready != 'R') { fprintf(stderr, "child setup failed\n"); waitpid(pid, &status, 0); return 1; }
	close(p[0]);
	usleep(300000);                     /* let it block in the wait */
	if (waitpid(pid, &status, WNOHANG) != 0) { fprintf(stderr, "%s %s: child did not block\n", timeline ? "timeline" : "binary", name); return 1; }
	t0 = now();
	kill(pid, sig);
	for (;;) {
		pid_t r = waitpid(pid, &status, WNOHANG);
		t = now() - t0;
		if (r == pid) break;
		if (t > 2.0) {
			printf("KILL_PROBE FAIL %s %s: not reaped after %.3f s (pid %d left; it may need a reboot)\n",
			    timeline ? "timeline" : "binary", name, t, (int)pid);
			return 1;
		}
		usleep(2000);
	}
	int good = sig == SIGUSR1 ? (WIFEXITED(status) && WEXITSTATUS(status) == 0) : (WIFSIGNALED(status) && WTERMSIG(status) == sig);
	printf("KILL_PROBE %s %s %s: reaped in %.1f ms, %s %d\n", good ? "PASS" : "FAIL", timeline ? "timeline" : "binary", name,
	    t * 1000, WIFSIGNALED(status) ? "signal" : "exit", WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status));
	return good ? 0 : 1;
}

int main(int argc, char **argv)
{
	char uuid[128] = { 0 }; size_t n = sizeof uuid; int failed = 0;
	if (argc != 4 || strlen(argv[1]) != 36 || strncmp(argv[2], "/dev/dri/", 9) || atoi(argv[3]) < 1) {
		fprintf(stderr, "usage: kill-probe KILLABLE_KERNELCACHE_UUID /dev/dri/renderD128 ROUNDS\n"); return 2;
	}
	if (sysctlbyname("kern.kernelcacheuuid", uuid, &n, NULL, 0) || strcasecmp(uuid, argv[1])) {
		fprintf(stderr, "refused: running kernel %s is not the killable kernel\n", uuid); return 2;
	}
	for (int round = 0; round < atoi(argv[3]) && !failed; round++)
		for (int timeline = 0; timeline < 2 && !failed; timeline++) {
			failed |= one(argv[2], timeline, SIGKILL);
			if (!failed) failed |= one(argv[2], timeline, SIGTERM);
			if (!failed) failed |= one(argv[2], timeline, SIGUSR1);
		}
	printf("KILL_PROBE %s\n", failed ? "FAIL" : "ALL PASS");
	return failed;
}
