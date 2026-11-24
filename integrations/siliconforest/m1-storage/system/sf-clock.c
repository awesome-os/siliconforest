// SPDX-License-Identifier: MIT
/*
 * sf-clock: the system's time from the machine's hardware clock, at boot.
 *
 * This kernel has no RTC service, so it starts at 1970. SFCpuPower (m1-native-boot/reboot/
 * SFAppleReboot.cpp) reads the SMC's counter and the offset Linux keeps in the PMU, as Linux
 * rtc-macsmc does, and offers the time as kern.sf_rtc_usec (microseconds since 1970 UTC, 0
 * until it has been read: the SMC session opens some seconds into the boot). This tool waits
 * for it and sets the kernel's calendar with settimeofday(2). Nothing is written to the
 * hardware: the kernel has no RTC to pass the new time on to.
 *
 *   sf-clock set [seconds to wait, default 30]   exit 0: set; 1: no time within the wait; 2: no such kernel
 *   sf-clock show                                what the kernel says about the clock
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>

static int64_t rtc_usec(int *missing)
{
	int64_t v = 0;
	size_t len = sizeof(v);

	*missing = 0;
	if (sysctlbyname("kern.sf_rtc_usec", &v, &len, NULL, 0) != 0) {
		*missing = errno == ENOENT;
		return 0;
	}
	return v;
}

int main(int argc, char **argv)
{
	char text[256] = "";
	size_t len = sizeof(text) - 1;
	struct timeval before, tv;
	int missing = 0, waited = 0, limit = 30000;
	int64_t now;

	if (argc >= 2 && !strcmp(argv[1], "show")) {
		if (sysctlbyname("kern.sf_rtc", text, &len, NULL, 0) != 0)
			snprintf(text, sizeof(text), "(kern.sf_rtc: %s)", strerror(errno));
		gettimeofday(&tv, NULL);
		printf("SF_CLOCK system=%lld hardware: %s\n", (long long)tv.tv_sec, text);
		return 0;
	}
	if (argc < 2 || strcmp(argv[1], "set")) {
		fprintf(stderr, "usage: sf-clock set [seconds] | sf-clock show\n");
		return 64;
	}
	if (argc >= 3)
		limit = atoi(argv[2]) * 1000;
	while ((now = rtc_usec(&missing)) == 0 && !missing && waited < limit) {
		usleep(100000);
		waited += 100;
	}
	if (missing) {
		printf("SF_CLOCK not set: this kernel has no kern.sf_rtc_usec\n");
		return 2;
	}
	if (now == 0) {
		if (sysctlbyname("kern.sf_rtc", text, &len, NULL, 0) != 0)
			text[0] = 0;
		printf("SF_CLOCK not set: no time from the hardware clock in %d ms (%s)\n", waited, text);
		return 1;
	}
	gettimeofday(&before, NULL);
	tv.tv_sec = (time_t)(now / 1000000);
	tv.tv_usec = (suseconds_t)(now % 1000000);
	if (settimeofday(&tv, NULL) != 0) {
		printf("SF_CLOCK not set: settimeofday: %s\n", strerror(errno));
		return 1;
	}
	printf("SF_CLOCK set utc=%lld waited_ms=%d before=%lld source=hardware-clock\n", (long long)tv.tv_sec, waited, (long long)before.tv_sec);
	return 0;
}
