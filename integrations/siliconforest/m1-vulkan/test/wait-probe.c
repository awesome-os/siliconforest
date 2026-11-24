// SPDX-License-Identifier: MIT
// wait-probe: do finite DRM syncobj waits wait? No GPU work: syncobjs only.
// Mesa (and Vulkan's runtime) passes DRM_IOCTL_SYNCOBJ_WAIT an ABSOLUTE deadline computed from
// userspace CLOCK_MONOTONIC; the kernel compares it with its own ktime_get(). On the M1 a 3 s
// vkWaitForFences returned VK_TIMEOUT at once. This measures, for an unsignalled syncobj (nothing
// will ever signal it), how long a wait for "now + 300 ms" really takes, with "now" read from
// each userspace clock, and how far the clocks are apart.
// Each line: "WAIT <clock> asked_ms=300 took_ms=<t> rc=<rc> errno=<e>"; a correct kernel and a
// matching clock give took_ms ~300 and errno ETIMEDOUT (60).
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>

static int64_t ns(clockid_t c)
{
	struct timespec ts;
	clock_gettime(c, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int main(void)
{
	int fd = -1;
	for (int i = 128; i < 136 && fd < 0; i++) {
		char p[32];
		snprintf(p, sizeof p, "/dev/dri/renderD%d", i);
		int f = open(p, O_RDWR | O_CLOEXEC);
		if (f < 0)
			continue;
		drmVersionPtr v = drmGetVersion(f);
		if (v && !strcmp(v->name, "asahi"))
			fd = f;
		else
			close(f);
		if (v)
			drmFreeVersion(v);
	}
	if (fd < 0) {
		printf("WAIT no asahi node\n");
		return 2;
	}
	printf("WAIT clocks_ns MONOTONIC=%lld MONOTONIC_RAW=%lld UPTIME_RAW=%lld REALTIME=%lld\n",
	       (long long)ns(CLOCK_MONOTONIC), (long long)ns(CLOCK_MONOTONIC_RAW),
	       (long long)ns(CLOCK_UPTIME_RAW), (long long)ns(CLOCK_REALTIME));
	struct { const char *name; clockid_t c; } clocks[] = {
		{ "MONOTONIC", CLOCK_MONOTONIC }, { "UPTIME_RAW", CLOCK_UPTIME_RAW },
	};
	uint32_t h;
	if (drmSyncobjCreate(fd, 0, &h)) {
		printf("WAIT create failed errno %d\n", errno);
		return 3;
	}
	int fails = 0;
	for (unsigned k = 0; k < 2; k++) {
		int64_t t0 = ns(CLOCK_UPTIME_RAW);
		int64_t deadline = ns(clocks[k].c) + 300000000;
		int r = drmSyncobjWait(fd, &h, 1, deadline, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);
		int e = r ? errno : 0;
		double took = (ns(CLOCK_UPTIME_RAW) - t0) / 1e6;
		printf("WAIT binary-unsubmitted %s asked_ms=300 took_ms=%.1f rc=%d errno=%d\n", clocks[k].name, took, r, e);
		if (took < 250 || took > 1000)
			fails++;
	}
	// a timeline point that is never submitted
	uint32_t t;
	uint64_t one = 1, two = 2;
	drmSyncobjCreate(fd, 0, &t);
	drmSyncobjTimelineSignal(fd, &t, &one, 1);
	int64_t t0 = ns(CLOCK_UPTIME_RAW);
	int r = drmSyncobjTimelineWait(fd, &t, &two, 1, ns(CLOCK_MONOTONIC) + 300000000,
				       DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);
	int e = r ? errno : 0;
	double took = (ns(CLOCK_UPTIME_RAW) - t0) / 1e6;
	printf("WAIT timeline-point2-unsubmitted MONOTONIC asked_ms=300 took_ms=%.1f rc=%d errno=%d\n", took, r, e);
	if (took < 250 || took > 1000)
		fails++;
	printf("WAIT done failures=%d\n", fails);
	return fails;
}
