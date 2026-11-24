// SPDX-License-Identifier: MIT
/*
 * sf-roinit: process 1 of a SiliconForest whose root filesystem is mounted read-only.
 * The kernel (DEVELOPMENT) starts /usr/appleinternal/sbin/launchd.<launchdsuffix> when that
 * boot argument is given: this program is installed there as launchd.sfro. It does the one
 * thing that has to precede every other process, then becomes the real launchd:
 *
 *   devfs on /dev (launchd would do it; the memory disk's node is needed now)
 *   the memory disk /dev/md0 (ext4, writable, RAM) on /private/ram; a root that has
 *   /etc/sf-state-devices names other candidates, one per line, tried in order (the QEMU
 *   rehearsal has no memory disk and uses a third virtio disk)
 *
 * The root filesystem's /var, /private/var, /private/tmp and /etc/resolv.conf are symbolic
 * links into /private/ram, so everything the system writes lands in memory and the disk is
 * never asked to write. If the mount fails the system still starts (those paths then dangle
 * and writes fail), with the reason on the console.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

struct generic_mount_args { char *fspec; };

static void say(const char *text, int err)
{
	char line[200];
	int fd = open("/dev/console", O_WRONLY | O_NOCTTY), n;
	n = err ? snprintf(line, sizeof(line), "sf-roinit: %s: %s\n", text, strerror(err)) : snprintf(line, sizeof(line), "sf-roinit: %s\n", text);
	if (fd >= 0) { (void)write(fd, line, (size_t)n); close(fd); }
}

int main(int argc, char **argv)
{
	char list[512] = "/dev/md0\n", *dev, *next;
	struct stat st;
	int fd, done = 0;
	(void)argc;
	if (mount("devfs", "/dev", 0, NULL) < 0 && errno != EBUSY)
		say("devfs on /dev", errno);
	if ((fd = open("/etc/sf-state-devices", O_RDONLY)) >= 0) {
		ssize_t n = read(fd, list, sizeof(list) - 1);
		list[n > 0 ? n : 0] = 0;
		close(fd);
	}
	for (int round = 0; round < 50 && !done; round++) {
		char copy[512];
		memcpy(copy, list, sizeof(copy));
		for (dev = copy; dev && *dev && !done; dev = next) {
			struct generic_mount_args args = { dev };
			if ((next = strchr(dev, '\n')) != NULL)
				*next++ = 0;
			if (stat(dev, &st) == 0 && mount("ext4", "/private/ram", 0, &args) == 0) {
				char text[160];
				snprintf(text, sizeof(text), "root read-only; %s on /private/ram holds /var and /tmp", dev);
				say(text, 0);
				done = 1;
			}
		}
		if (!done)
			usleep(100000);
	}
	if (!done)
		say("NO WRITABLE STATE: nothing mounted on /private/ram", errno);
	execv("/sbin/launchd", argv);
	say("exec /sbin/launchd", errno);
	return 1;
}
