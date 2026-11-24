/* SPDX-License-Identifier: MIT */
/*
 * Workloads shaped like the M1's root, for a QEMU scratch ext4 only (run-qemu.py). The host
 * runs Linux e2fsck -fn on a copy of the filesystem taken after every step.
 *
 *   fsck-load <mounted scratch directory> <step> <seed>
 *     pair    two inode allocations in one journal transaction (the smallest reproduction)
 *     htree   a create, a delete and a rename in two directories indexed by Linux
 *     small   many small files in several directories, half of them deleted again
 *     cache   a browser-profile-like cache: writes, atomic replacements by rename, deletes,
 *             files deleted while open
 *     fill    large files and then small ones until the filesystem is full (ENOSPC)
 *     purge   everything of the steps before deleted in one go
 *     hold    four files deleted while open, the process stays (sealed with an orphan list)
 */
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint64_t rng;
static uint32_t rnd(void) { rng = rng * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(rng >> 33); }
static void fail(const char *what, const char *path)
{
	printf("FSCK_LOAD FAIL %s %s errno=%d (%s)\n", what, path ? path : "", errno, strerror(errno));
	exit(1);
}
static char buf[1 << 20];

/* 0, or errno when the filesystem is full */
static int put(const char *path, size_t size)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	size_t done = 0;

	if (fd < 0) {
		if (errno == ENOSPC) return ENOSPC;
		fail("create", path);
	}
	while (done < size) {
		size_t n = size - done > sizeof(buf) ? sizeof(buf) : size - done;
		ssize_t w = write(fd, buf, n);
		if (w < 0) {
			int e = errno;
			close(fd);
			if (e == ENOSPC) return ENOSPC;
			errno = e; fail("write", path);
		}
		if (w == 0) break;
		done += (size_t)w;
	}
	if (close(fd)) fail("close", path);
	return 0;
}

static unsigned long purge(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	unsigned long n = 0;
	char path[1024];

	if (d == NULL) {
		if (errno == ENOENT) return 0;
		fail("opendir", dir);
	}
	/* names first: the directory changes while it is emptied */
	char (*names)[256] = NULL; size_t count = 0, room = 0;
	while ((e = readdir(d)) != NULL) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (count == room) { room = room ? room * 2 : 256; names = realloc(names, room * 256); if (!names) fail("memory", dir); }
		strlcpy(names[count++], e->d_name, 256);
	}
	closedir(d);
	for (size_t i = 0; i < count; i++) {
		struct stat st;
		snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
		if (lstat(path, &st)) fail("lstat", path);
		if (S_ISDIR(st.st_mode)) n += purge(path);
		else { if (unlink(path)) fail("unlink", path); n++; }
	}
	free(names);
	if (rmdir(dir)) fail("rmdir", dir);
	return n + 1;
}

int main(int argc, char **argv)
{
	char args[1024] = { 0 }, path[1024], other[1024];
	size_t n = sizeof(args);
	struct statfs fs;

	setbuf(stdout, NULL);
	if (argc != 4) { fprintf(stderr, "usage: fsck-load <directory> <step> <seed>\n"); return 64; }
	if (sysctlbyname("kern.bootargs", args, &n, NULL, 0) || !strstr(args, "sfqemu_fsck=1")) {
		fprintf(stderr, "REFUSED: the QEMU scratch boot flag is required\n"); return 77;
	}
	if (statfs(argv[1], &fs) || strcmp(fs.f_fstypename, "ext4") || !strcmp(fs.f_mntonname, "/")) {
		fprintf(stderr, "REFUSED: %s is not a mounted scratch ext4\n", argv[1]); return 77;
	}
	const char *root = argv[1], *step = argv[2];
	rng = strtoull(argv[3], NULL, 0) * 2654435761u + 12345;
	for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (char)(i * 31 + (i >> 9));

	if (!strcmp(step, "pair")) {
		for (int i = 0; i < 4; i++) {
			snprintf(path, sizeof(path), "%s/pair-%d", root, i);
			if (put(path, 0)) fail("pair", path);
		}
		printf("FSCK_LOAD pair files=4\n");
	} else if (!strcmp(step, "htree")) {
		/* two directories that Linux made and indexed (run-qemu.py): h1 one index level, h2 two.
		 * Names as the runner made them: eNNNNN, in h2 padded with 'x' to 250 characters. */
		for (int d = 1; d <= 2; d++) {
			char pad[256] = "";
			if (d == 2) { memset(pad, 'x', 244); pad[244] = 0; }
			snprintf(path, sizeof(path), "%s/h%d/new-%u", root, d, rnd() % 1000);
			if (put(path, 100)) fail("htree create", path);
			snprintf(path, sizeof(path), "%s/h%d/e%05u%s", root, d, 1 + rnd() % 100, pad);
			if (unlink(path)) fail("htree unlink", path);
			snprintf(path, sizeof(path), "%s/h%d/e%05u%s", root, d, 101 + rnd() % 100, pad);
			snprintf(other, sizeof(other), "%s/h%d/moved", root, d);
			if (rename(path, other)) fail("htree rename", path);
		}
		printf("FSCK_LOAD htree directories=2\n");
	} else if (!strcmp(step, "small")) {
		unsigned made = 0, gone = 0;
		snprintf(path, sizeof(path), "%s/small", root);
		if (mkdir(path, 0755)) fail("mkdir", path);
		for (int d = 0; d < 20; d++) {
			snprintf(path, sizeof(path), "%s/small/d%02d", root, d);
			if (mkdir(path, 0755)) fail("mkdir", path);
			for (int f = 0; f < 100; f++) {
				snprintf(path, sizeof(path), "%s/small/d%02d/f%03d", root, d, f);
				if (put(path, rnd() % 3000)) fail("small", path);
				made++;
			}
		}
		for (int d = 0; d < 20; d++)
			for (int f = (int)(rnd() & 1); f < 100; f += 2) {
				snprintf(path, sizeof(path), "%s/small/d%02d/f%03d", root, d, f);
				if (unlink(path)) fail("unlink", path);
				gone++;
			}
		printf("FSCK_LOAD small made=%u deleted=%u\n", made, gone);
	} else if (!strcmp(step, "cache")) {
		int held[8]; unsigned next = 0, writes = 0, renames = 0, unlinks = 0, heldn = 0;
		for (int i = 0; i < 8; i++) held[i] = -1;
		snprintf(path, sizeof(path), "%s/cache", root);
		if (mkdir(path, 0755)) fail("mkdir", path);
		for (int op = 0; op < 3000; op++) {
			unsigned k = rnd() % 400, what = rnd() % 10;
			snprintf(path, sizeof(path), "%s/cache/e%03u", root, k);
			if (what < 4) {
				if (put(path, rnd() % 65536)) fail("cache write", path);
				writes++;
			} else if (what < 7) {
				snprintf(other, sizeof(other), "%s/cache/e%03u.tmp", root, k);
				if (put(other, rnd() % 20000)) fail("cache tmp", other);
				if (rename(other, path)) fail("rename", path);
				renames++;
			} else if (what < 9) {
				if (unlink(path) == 0) unlinks++;
				else if (errno != ENOENT) fail("unlink", path);
			} else {
				int fd = open(path, O_RDWR);
				if (fd < 0) { if (errno != ENOENT) fail("open", path); continue; }
				if (unlink(path)) fail("unlink open", path);
				if (write(fd, buf, 5000) < 0) fail("write unlinked", path);
				if (held[next] >= 0) close(held[next]);
				held[next] = fd; next = (next + 1) % 8; heldn++;
			}
		}
		for (int i = 0; i < 8; i++) if (held[i] >= 0) close(held[i]);
		printf("FSCK_LOAD cache writes=%u renames=%u unlinks=%u unlinked_open=%u\n", writes, renames, unlinks, heldn);
	} else if (!strcmp(step, "hold")) {
		/* stays alive with four deleted files open: the filesystem is sealed with them on the orphan list */
		for (int i = 0; i < 4; i++) {
			int fd;
			snprintf(path, sizeof(path), "%s/held-%d", root, i);
			if (put(path, 20000 + rnd() % 50000)) fail("hold", path);
			if ((fd = open(path, O_RDWR)) < 0 || unlink(path)) fail("hold open", path);
		}
		printf("FSCK_LOAD hold files=4\n");
		for (;;) pause();
	} else if (!strcmp(step, "fill")) {
		unsigned big = 0, small = 0, dirs = 0;
		snprintf(path, sizeof(path), "%s/fill", root);
		if (mkdir(path, 0755)) fail("mkdir", path);
		for (;; big++) {
			snprintf(path, sizeof(path), "%s/fill/big-%04u", root, big);
			if (put(path, (1 + rnd() % 4) << 20)) break;
		}
		for (; small < 20000; small++) {
			snprintf(path, sizeof(path), "%s/fill/s-%05u", root, small);
			if (put(path, 100 + rnd() % 8000)) break;
		}
		for (; dirs < 2000; dirs++) {
			snprintf(path, sizeof(path), "%s/fill/dir-%04u", root, dirs);
			if (mkdir(path, 0755)) { if (errno == ENOSPC) break; fail("mkdir", path); }
		}
		/* appends and replacements on the full filesystem, as the M1's programs kept trying */
		unsigned refused = 0;
		for (int i = 0; i < 200; i++) {
			snprintf(path, sizeof(path), "%s/fill/late-%03d", root, i);
			if (put(path, 30000)) refused++;
			snprintf(other, sizeof(other), "%s/fill/s-%05u", root, rnd() % (small ? small : 1));
			if (rename(path, other) && errno != ENOENT && errno != ENOSPC) fail("rename full", path);
		}
		if (statfs(root, &fs)) fail("statfs", root);
		printf("FSCK_LOAD fill big=%u small=%u dirs=%u refused=%u free_blocks=%llu\n", big, small, dirs, refused,
		    (unsigned long long)fs.f_bfree);
	} else if (!strcmp(step, "purge")) {
		unsigned long gone = 0;
		const char *names[] = { "fill", "small", "cache" };
		for (int i = 0; i < 3; i++) { snprintf(path, sizeof(path), "%s/%s", root, names[i]); gone += purge(path); }
		printf("FSCK_LOAD purge removed=%lu\n", gone);
	} else {
		fprintf(stderr, "unknown step %s\n", step); return 64;
	}
	return 0;
}
