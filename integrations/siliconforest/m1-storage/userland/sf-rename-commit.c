/* SPDX-License-Identifier: MIT */
/*
 * sf-rename-commit - renames a list of files over their targets in one process.
 *
 *   sf-rename-commit LIST      LIST: one "FROM<tab>TO" per line; rename(FROM, TO) for each, in order
 *   sf-rename-commit --check   exits 0 (the caller's test that this program runs here)
 *
 * For tools/sf_rename_switch.sh and tools/sf_rename_restore.sh on a running system: libSystem
 * and /usr/lib/system/libdyld.dylib name each other's entry points, so a program started while
 * one of them is new and the other old does not load. This program is started while they still
 * match, has everything it needs loaded by then, and starts nothing: the files change under it
 * and it only leaves. It stops at the first rename that fails (the lines before it are done,
 * the ones after it are not) and says which.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char text[1 << 20];

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--check") == 0)
		return 0;
	if (argc != 2) {
		fprintf(stderr, "usage: sf-rename-commit LIST | --check\n");
		return 64;
	}
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "sf-rename-commit: %s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	size_t have = 0;
	for (;;) {
		ssize_t n = read(fd, text + have, sizeof(text) - 1 - have);
		if (n < 0) {
			fprintf(stderr, "sf-rename-commit: read: %s\n", strerror(errno));
			return 1;
		}
		if (n == 0)
			break;
		have += (size_t)n;
		if (have == sizeof(text) - 1) {
			fprintf(stderr, "sf-rename-commit: the list is too long\n");
			return 1;
		}
	}
	close(fd);
	text[have] = '\0';
	/* the whole list is checked before the first rename */
	int lines = 0;
	for (char *p = text; *p != '\0';) {
		char *end = strchr(p, '\n');
		char *tab = strchr(p, '\t');
		if (end == NULL || tab == NULL || tab > end || tab == p || tab + 1 == end) {
			fprintf(stderr, "sf-rename-commit: line %d is not FROM<tab>TO\n", lines + 1);
			return 1;
		}
		lines++;
		p = end + 1;
	}
	int done = 0;
	for (char *p = text; *p != '\0';) {
		char *end = strchr(p, '\n');
		char *tab = strchr(p, '\t');
		*tab = '\0';
		*end = '\0';
		if (rename(p, tab + 1) != 0) {
			int e = errno;
			fprintf(stderr, "sf-rename-commit: %s -> %s: %s (%d of %d done)\n", p, tab + 1, strerror(e), done, lines);
			return 1;
		}
		done++;
		p = end + 1;
	}
	sync();
	return 0;
}
