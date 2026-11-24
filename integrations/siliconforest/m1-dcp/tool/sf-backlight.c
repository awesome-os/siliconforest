// SPDX-License-Identifier: MIT
/*
 * sf-backlight - the panel backlight from the command line.
 *
 *   sf-backlight get              one line: percent, level, maximum, the level the display
 *                                 reports, and power (0 on, 4 off)
 *   sf-backlight set <percent>    0..100 of the maximum (0 is the lowest level, not off)
 *   sf-backlight up | down        5 percent steps; down stops at the lowest level
 *   sf-backlight raw <level>      a level in the device's own units (nits)
 *   sf-backlight power on|off     the backlight off and on again at the level it had
 *
 * It reads and writes the sysctls kern.sf_backlight.{brightness,max_brightness,
 * actual_brightness,bl_power} of the SFDcp kext, which are the files Linux has under
 * /sys/class/backlight/apple-panel-bl. A program can do the same with sysctlbyname(3).
 * Setting needs root. Exit status 0, or 1 with the failing call and errno on stderr.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>

static int get(const char *name, int *value)
{
	size_t len = sizeof(*value);

	if (sysctlbyname(name, value, &len, NULL, 0)) {
		fprintf(stderr, "sf-backlight: read of %s: errno %d (%s)\n", name, errno, strerror(errno));
		return 1;
	}
	return 0;
}

static int set(const char *name, int value)
{
	if (sysctlbyname(name, NULL, NULL, &value, sizeof(value))) {
		fprintf(stderr, "sf-backlight: write of %d to %s: errno %d (%s)\n", value, name, errno, strerror(errno));
		return 1;
	}
	return 0;
}

static int show(void)
{
	int level, max, actual = -1, power;

	if (get("kern.sf_backlight.brightness", &level) || get("kern.sf_backlight.max_brightness", &max) ||
	    get("kern.sf_backlight.bl_power", &power))
		return 1;
	if (get("kern.sf_backlight.actual_brightness", &actual))
		actual = -1;
	printf("sf-backlight: %d%% level %d of %d, display reports %d, power %d\n",
	       max > 0 ? (level * 100 + max / 2) / max : 0, level, max, actual, power);
	return 0;
}

int main(int argc, char **argv)
{
	int level, max, step, lowest = 1;

	if (argc < 2 || !strcmp(argv[1], "get"))
		return show();
	if (get("kern.sf_backlight.brightness", &level) || get("kern.sf_backlight.max_brightness", &max))
		return 1;
	step = max / 20 > 0 ? max / 20 : 1;
	if (!strcmp(argv[1], "set") && argc > 2) {
		int pct = atoi(argv[2]);

		if (pct < 0 || pct > 100) {
			fprintf(stderr, "sf-backlight: percent is 0..100\n");
			return 1;
		}
		level = (max * pct + 50) / 100;
		if (level < lowest)
			level = lowest;
	} else if (!strcmp(argv[1], "up")) {
		level = level + step > max ? max : level + step;
	} else if (!strcmp(argv[1], "down")) {
		level = level - step < lowest ? lowest : level - step;
	} else if (!strcmp(argv[1], "raw") && argc > 2) {
		level = atoi(argv[2]);
	} else if (!strcmp(argv[1], "power") && argc > 2) {
		if (set("kern.sf_backlight.bl_power", !strcmp(argv[2], "off") ? 4 : 0))
			return 1;
		return show();
	} else {
		fprintf(stderr, "usage: sf-backlight get | set <percent> | up | down | raw <level> | power on|off\n");
		return 1;
	}
	if (set("kern.sf_backlight.brightness", level))
		return 1;
	return show();
}
