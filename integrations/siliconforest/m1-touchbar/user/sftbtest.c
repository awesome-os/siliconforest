/* SPDX-License-Identifier: MIT */
/*
 * sftbtest: touches the Touch Bar's keys without a finger and reads what arrives as key
 * presses. It holds /dev/usb_hid_kbd (the SFHID key queue the compositor reads) open,
 * looks the keys' positions up in kern.sf_touchbar.keys, writes finger records in the
 * digitizer's units to kern.sf_touchbar.inject (the driver parses them as it parses the
 * digitizer's reports) and prints every key record it reads.
 *
 *   sftbtest [--label NAME] step...
 *     tap:<usage hex>    finger down on that key's middle, a small move, finger up
 *     hold:<usage hex>   finger down and left there (finger number = step number)
 *     lift               every finger of earlier hold steps up
 *     release            every finger of earlier hold steps up, expecting no key event
 *     up:<usage hex>     expect that key's release without doing anything (a layer change or
 *                        the held-key failsafe let it go)
 *     fn:<0|1>           the keyboard's Fn key up or down: kern.sf_touchbar.kbd_report makes the
 *                        keyboard's driver take a made-up keyboard report with that Fn bit
 *     slide:<a>:<b>      down on key a, move to key b, up
 *     dark:<usage hex>   tap a key expecting no key event (the strip is dark)
 *     wait:<ms>
 *     crc                prints kern.sf_touchbar.fb_crc (the picture the panel scans out now)
 * The last line is "TBTEST <label> expected=... got=... result=PASS|FAILED": the records read
 * against the presses and releases the steps stand for.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define MAXEV 256
static int fd = -1;
static unsigned got[MAXEV], ngot, want[MAXEV], nwant;

static void
drain(int ms)
{
	for (int t = 0; t <= ms; t += 5) {
		uint8_t r[8];
		ssize_t n;
		while ((n = read(fd, r, sizeof(r))) == (ssize_t)sizeof(r)) {
			printf("TBKEY seq=%u usage=%02x down=%u\n", (unsigned)(r[0] | r[1] << 8 | r[2] << 16 | (unsigned)r[3] << 24), r[4], r[5]);
			if (ngot < MAXEV)
				got[ngot++] = (unsigned)(r[4] << 1 | (r[5] & 1));
		}
		if (n > 0)
			printf("TBTEST short record of %zd bytes\n", n);
		usleep(5000);
	}
}

static void
expect(unsigned usage, int down)
{
	if (nwant < MAXEV)
		want[nwant++] = usage << 1 | (unsigned)down;
}

static int
key_x(unsigned usage)
{
	char text[1024];
	size_t size = sizeof(text) - 1;

	if (sysctlbyname("kern.sf_touchbar.keys", text, &size, NULL, 0) != 0)
		return -1;
	text[size] = 0;
	for (char *p = strtok(text, " "); p != NULL; p = strtok(NULL, " ")) {
		unsigned u, x;
		if (sscanf(p, "%x:%u", &u, &x) == 2 && u == usage)
			return (int)x;
	}
	return -1;
}

static int
inject(unsigned finger, unsigned state, int x)
{
	char text[64];

	snprintf(text, sizeof(text), "%u %u %d 320", finger, state, x);
	if (sysctlbyname("kern.sf_touchbar.inject", NULL, NULL, text, strlen(text) + 1) != 0) {
		printf("TBTEST inject \"%s\": errno %d\n", text, errno);
		return -1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	const char *label = "keys";
	int first = 1, failed = 0;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc > 2 && strcmp(argv[1], "--label") == 0) {
		label = argv[2];
		first = 3;
	}
	fd = open("/dev/usb_hid_kbd", O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		printf("TBTEST %s /dev/usb_hid_kbd: errno %d result=FAILED\n", label, errno);
		return 1;
	}
	drain(50);
	ngot = 0;
	for (int i = first; i < argc && !failed; i++) {
		unsigned a = 0, b = 0, finger = (unsigned)(i - first + 1);
		int x, x2;

		if (sscanf(argv[i], "tap:%x", &a) == 1 || sscanf(argv[i], "dark:%x", &a) == 1) {
			x = key_x(a);
			failed = x < 0 || inject(finger, 3, x) != 0;
			drain(60);
			failed = failed || inject(finger, 4, x + 40) != 0;
			drain(40);
			failed = failed || inject(finger, 7, x + 40) != 0;
			drain(150);
			if (argv[i][0] == 't') {
				expect(a, 1);
				expect(a, 0);
			}
		} else if (sscanf(argv[i], "hold:%x", &a) == 1) {
			x = key_x(a);
			failed = x < 0 || inject(finger, 3, x) != 0;
			drain(100);
			expect(a, 1);
			/* remembered for lift: the usage in the high bits of want is enough to release in order */
		} else if (strcmp(argv[i], "release") == 0) {
			for (int j = first; j < i; j++) {
				if (sscanf(argv[j], "hold:%x", &a) == 1)
					failed = failed || inject((unsigned)(j - first + 1), 7, 0) != 0;
			}
			drain(150);
		} else if (sscanf(argv[i], "up:%x", &a) == 1) {
			expect(a, 0);
		} else if (sscanf(argv[i], "fn:%u", &a) == 1) {
			int value = a ? 1 : 0;
			if (sysctlbyname("kern.sf_touchbar.kbd_report", NULL, NULL, &value, sizeof(value)) != 0) {
				printf("TBTEST kern.sf_touchbar.kbd_report=%d: errno %d\n", value, errno);
				failed = 1;
			}
			drain(150);
		} else if (strcmp(argv[i], "lift") == 0) {
			for (int j = first; j < i; j++) {
				if (sscanf(argv[j], "hold:%x", &a) == 1) {
					failed = failed || inject((unsigned)(j - first + 1), 7, 0) != 0;
					expect(a, 0);
				}
			}
			drain(150);
		} else if (sscanf(argv[i], "slide:%x:%x", &a, &b) == 2) {
			x = key_x(a);
			x2 = key_x(b);
			failed = x < 0 || x2 < 0 || inject(finger, 3, x) != 0;
			drain(60);
			failed = failed || inject(finger, 4, x2) != 0;
			drain(60);
			failed = failed || inject(finger, 7, x2) != 0;
			drain(150);
			expect(a, 1);
			expect(a, 0);
		} else if (strcmp(argv[i], "crc") == 0) {
			char crc[32] = "";
			size_t size = sizeof(crc) - 1;
			sysctlbyname("kern.sf_touchbar.fb_crc", crc, &size, NULL, 0);
			printf("TBCRC %s\n", crc);
		} else if (sscanf(argv[i], "wait:%u", &a) == 1) {
			drain((int)a);
		} else {
			printf("TBTEST unknown step %s\n", argv[i]);
			failed = 1;
		}
		if (failed)
			printf("TBTEST step %s could not be carried out\n", argv[i]);
	}
	drain(200);
	printf("TBTEST %s expected=", label);
	for (unsigned i = 0; i < nwant; i++)
		printf("%s%02x%c", i ? "," : "", want[i] >> 1, (want[i] & 1) ? 'd' : 'u');
	printf(" got=");
	for (unsigned i = 0; i < ngot; i++)
		printf("%s%02x%c", i ? "," : "", got[i] >> 1, (got[i] & 1) ? 'd' : 'u');
	failed = failed || ngot != nwant || memcmp(got, want, ngot * sizeof(got[0])) != 0;
	printf(" result=%s\n", failed ? "FAILED" : "PASS");
	close(fd);
	return failed;
}
