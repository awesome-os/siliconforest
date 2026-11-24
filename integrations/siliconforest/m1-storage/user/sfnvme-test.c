// SPDX-License-Identifier: MIT
/*
 * sfnvme-test: reads of the internal NVMe storage through SFAppleNVMe, for the bench.
 * It opens devices read-only, except in `wtest`, whose whole point is that the attempt
 * to open for writing fails.
 *
 *   sfnvme-test hash <device> <rangefile>        SHA-256 of each "<lba> <blocks>" line (4096-byte blocks)
 *   sfnvme-test seq <device> <lba> <MiB> <KiB>   sequential read, <KiB> per read call: MB/s
 *   sfnvme-test rand <device> <reads> <blocks>   4096-byte reads at random blocks below <blocks>: latency
 *   sfnvme-test wtest <device>                   opening for writing must fail; reports how
 *   sfnvme-test head <device> <lba>              the first 64 bytes of a block in hexadecimal
 *
 *   sfnvme-test tree|treex <dir> [file ...]      digest of a directory tree (treex: one filesystem)
 *   sfnvme-test stopreport <nonce>               stop the controller and report, from memory
 *
 * The same source builds on Linux (cc -O2 -o sfnvme-test sfnvme-test.c) to compute the
 * reference hashes with identical code: there the device is opened with O_DIRECT.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define BLOCK 4096

/* ---- SHA-256 (FIPS 180-4) ---- */
struct sha256 { uint32_t h[8]; uint64_t len; uint8_t buf[64]; unsigned n; };
static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
	0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
	0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(struct sha256 *s, const uint8_t *p)
{
	uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
	int i;
	for (i = 0; i < 16; i++)
		w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
	for (; i < 64; i++)
		w[i] = (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7] +
		    (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
	a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
	for (i = 0; i < 64; i++) {
		t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha_init(struct sha256 *s)
{
	static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	memcpy(s->h, iv, sizeof(iv));
	s->len = 0;
	s->n = 0;
}
static void sha_update(struct sha256 *s, const uint8_t *p, size_t len)
{
	s->len += len;
	while (len > 0) {
		if (s->n == 0 && len >= 64) {
			sha_block(s, p);
			p += 64;
			len -= 64;
			continue;
		}
		size_t take = 64 - s->n < len ? 64 - s->n : len;
		memcpy(s->buf + s->n, p, take);
		s->n += (unsigned)take;
		p += take;
		len -= take;
		if (s->n == 64) {
			sha_block(s, s->buf);
			s->n = 0;
		}
	}
}
static void sha_final(struct sha256 *s, char hex[65])
{
	uint64_t bits = s->len * 8;
	uint8_t pad[72] = { 0x80 }, lenb[8];
	size_t padlen = (s->n < 56 ? 56 : 120) - s->n;
	int i;
	for (i = 0; i < 8; i++)
		lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
	sha_update(s, pad, padlen);
	sha_update(s, lenb, 8);
	for (i = 0; i < 8; i++)
		snprintf(hex + 8 * i, 9, "%08x", s->h[i]);
}

static double now(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

static int open_ro(const char *path)
{
	int flags = O_RDONLY;
#ifdef O_DIRECT
	flags |= O_DIRECT;
#endif
	int fd = open(path, flags);
	if (fd < 0)
		printf("NVT_FAIL open %s: %s\n", path, strerror(errno));
	return fd;
}

static uint8_t *buffer(size_t bytes)
{
	void *p = NULL;
	if (posix_memalign(&p, 16384, bytes) != 0)
		return NULL;
	return p;
}

/* Reads exactly `bytes` at `offset`; short reads are continued. */
static int read_full(int fd, uint8_t *buf, size_t bytes, uint64_t offset)
{
	size_t done = 0;
	while (done < bytes) {
		ssize_t n = pread(fd, buf + done, bytes - done, (off_t)(offset + done));
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return -1;
		done += (size_t)n;
	}
	return 0;
}

static int cmd_hash(const char *dev, const char *rangefile)
{
	FILE *f = fopen(rangefile, "r");
	int fd = open_ro(dev);
	uint8_t *buf = buffer(1 << 20);
	unsigned long long lba, blocks;
	unsigned ranges = 0, bad = 0;
	struct sha256 all;
	char hex[65];

	if (f == NULL || fd < 0 || buf == NULL) {
		printf("NVT_FAIL hash: cannot open %s or %s\n", dev, rangefile);
		return 1;
	}
	sha_init(&all);
	while (fscanf(f, "%llu %llu", &lba, &blocks) == 2) {
		struct sha256 s;
		unsigned long long done = 0;
		int err = 0;

		sha_init(&s);
		while (done < blocks && !err) {
			unsigned long long n = blocks - done > 256 ? 256 : blocks - done;
			if (read_full(fd, buf, (size_t)n * BLOCK, (lba + done) * BLOCK) != 0) {
				err = errno ? errno : EIO;
				break;
			}
			sha_update(&s, buf, (size_t)n * BLOCK);
			sha_update(&all, buf, (size_t)n * BLOCK);
			done += n;
		}
		ranges++;
		if (err) {
			bad++;
			printf("NVT_HASH %llu %llu ERROR %s\n", lba, blocks, strerror(err));
		} else {
			sha_final(&s, hex);
			printf("NVT_HASH %llu %llu %s\n", lba, blocks, hex);
		}
	}
	sha_final(&all, hex);
	printf("NVT_HASH_ALL ranges=%u errors=%u %s\n", ranges, bad, hex);
	return bad != 0;
}

static int cmd_seq(const char *dev, unsigned long long lba, unsigned long long mib, unsigned long long kib)
{
	int fd = open_ro(dev);
	size_t chunk = (size_t)kib * 1024;
	uint8_t *buf = buffer(chunk);
	unsigned long long total = mib << 20, done = 0;
	struct sha256 s;
	char hex[65];
	double t0, t1, hashed;

	if (fd < 0 || buf == NULL || chunk == 0 || chunk % BLOCK)
		return 1;
	/* timed without hashing, then once more with it for a checksum of what was read */
	t0 = now();
	while (done < total) {
		if (read_full(fd, buf, chunk, lba * BLOCK + done) != 0) {
			printf("NVT_FAIL seq read at byte %llu: %s\n", done, strerror(errno));
			return 1;
		}
		done += chunk;
	}
	t1 = now();
	printf("NVT_SEQ lba=%llu bytes=%llu chunk_kib=%llu seconds=%.3f mb_per_s=%.1f\n", lba, done, kib, t1 - t0,
	    (double)done / 1e6 / (t1 - t0));
	sha_init(&s);
	t0 = now();
	for (done = 0; done < total; done += chunk) {
		if (read_full(fd, buf, chunk, lba * BLOCK + done) != 0)
			return 1;
		sha_update(&s, buf, chunk);
	}
	hashed = now() - t0;
	sha_final(&s, hex);
	printf("NVT_SEQ_HASH lba=%llu blocks=%llu %s (second pass, %.3f s with hashing)\n", lba, total / BLOCK, hex, hashed);
	return 0;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

static int cmd_rand(const char *dev, unsigned reads, unsigned long long blocks)
{
	int fd = open_ro(dev);
	uint8_t *buf = buffer(BLOCK);
	double *lat = calloc(reads, sizeof(double)), sum = 0, t0;
	uint64_t x = 0x2545f4914f6cdd1dULL;
	unsigned i;

	if (fd < 0 || buf == NULL || lat == NULL || reads == 0 || blocks == 0)
		return 1;
	t0 = now();
	for (i = 0; i < reads; i++) {
		double a;
		x ^= x << 13; x ^= x >> 7; x ^= x << 17;
		a = now();
		if (read_full(fd, buf, BLOCK, (x % blocks) * BLOCK) != 0) {
			printf("NVT_FAIL rand read %u at block %llu: %s\n", i, (unsigned long long)(x % blocks), strerror(errno));
			return 1;
		}
		lat[i] = now() - a;
		sum += lat[i];
	}
	t0 = now() - t0;
	qsort(lat, reads, sizeof(double), cmp_double);
	printf("NVT_RAND reads=%u span_blocks=%llu seconds=%.3f iops=%.0f avg_us=%.0f p50_us=%.0f p99_us=%.0f max_us=%.0f\n", reads,
	    blocks, t0, reads / t0, sum / reads * 1e6, lat[reads / 2] * 1e6, lat[reads - 1 - reads / 100] * 1e6, lat[reads - 1] * 1e6);
	return 0;
}

/* Never built for Linux: on the M1's Linux this tool must not be able to open the disk for writing at all. */
#ifdef __APPLE__
static int cmd_wtest(const char *dev)
{
	static const struct { int flags; const char *name; } modes[] = { { O_RDWR, "O_RDWR" }, { O_WRONLY, "O_WRONLY" } };
	int clean = 1;
	unsigned i;

	for (i = 0; i < 2; i++) {
		int fd = open(dev, modes[i].flags);
		if (fd < 0) {
			printf("NVT_WTEST %s open(%s) refused: %s\n", dev, modes[i].name, strerror(errno));
			continue;
		}
		/*
		 * The open was granted. The write below rewrites block 0 with the bytes just read
		 * from block 0, so even a driver that carried it out would change nothing; the
		 * read-only driver must refuse it.
		 */
		uint8_t *buf = buffer(BLOCK);
		int rfd = open(dev, O_RDONLY);
		if (buf == NULL || rfd < 0 || read_full(rfd, buf, BLOCK, 0) != 0) {
			printf("NVT_WTEST %s open(%s) GRANTED; block 0 could not be read back, nothing written\n", dev, modes[i].name);
			clean = 0;
		} else {
			ssize_t n = pwrite(fd, buf, BLOCK, 0);
			if (n < 0) {
				printf("NVT_WTEST %s open(%s) granted, write refused: %s\n", dev, modes[i].name, strerror(errno));
			} else {
				printf("NVT_WTEST %s open(%s) GRANTED AND WRITE RETURNED %zd\n", dev, modes[i].name, n);
				clean = 0;
			}
		}
		if (rfd >= 0)
			close(rfd);
		close(fd);
	}
	printf("NVT_WTEST_RESULT %s %s\n", dev, clean ? "writes-refused" : "WRITE-PATH-OPEN");
	return !clean;
}
#endif

static int cmd_head(const char *dev, unsigned long long lba)
{
	int fd = open_ro(dev);
	uint8_t *buf = buffer(BLOCK);
	int i;

	if (fd < 0 || buf == NULL || read_full(fd, buf, BLOCK, lba * BLOCK) != 0) {
		printf("NVT_FAIL head %s block %llu: %s\n", dev, lba, strerror(errno));
		return 1;
	}
	printf("NVT_HEAD %s %llu ", dev, lba);
	for (i = 0; i < 64; i++)
		printf("%02x", buf[i]);
	printf("\n");
	return 0;
}


/*
 * tree <dir> [file ...]: one digest over a whole directory tree, for the comparison of a
 * mounted filesystem with the image it was copied from (this source built on the host computes the same
 * over a copy taken out of the image). Entries in byte order of their path relative to <dir>; each contributes
 * "<type> <path>\0" and, for a regular file, its size and SHA-256, for a symbolic link its
 * target. Also one digest per top-level entry, and the SHA-256 of each named file.
 */
struct ent { char *path; char type; };
static struct ent *ents; static size_t nents, cap;
static int one_fs; static dev_t root_dev;       /* treex: directories on another filesystem are listed, not entered */
static int walk(const char *root, const char *rel)
{
	char full[2048];
	snprintf(full, sizeof(full), "%s/%s", root, rel);
	DIR *d = opendir(full);
	if (!d) { printf("NVT_TREE_FAIL opendir %s: %s\n", full, strerror(errno)); return 1; }
	struct dirent *e; int bad = 0;
	while ((e = readdir(d)) != NULL) {
		char child[2048]; struct stat st;
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		snprintf(child, sizeof(child), "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
		snprintf(full, sizeof(full), "%s/%s", root, child);
		if (lstat(full, &st) != 0) { printf("NVT_TREE_FAIL lstat %s: %s\n", full, strerror(errno)); bad = 1; continue; }
		if (nents == cap) { cap = cap ? cap * 2 : 4096; ents = realloc(ents, cap * sizeof(*ents)); if (!ents) return 1; }
		ents[nents].path = strdup(child);
		ents[nents].type = S_ISREG(st.st_mode) ? 'f' : S_ISDIR(st.st_mode) ? 'd' : S_ISLNK(st.st_mode) ? 'l' : 'o';
		nents++;
		if (S_ISDIR(st.st_mode) && !(one_fs && st.st_dev != root_dev)) bad |= walk(root, child);
	}
	closedir(d);
	return bad;
}
static int cmp_ent(const void *a, const void *b) { return strcmp(((const struct ent *)a)->path, ((const struct ent *)b)->path); }
static int file_sha(const char *path, char hex[65], unsigned long long *size)
{
	static uint8_t buf[1 << 20];
	struct sha256 s; ssize_t n; int fd = open(path, O_RDONLY);
	if (fd < 0) return 1;
	sha_init(&s); *size = 0;
	while ((n = read(fd, buf, sizeof(buf))) > 0) { sha_update(&s, buf, (size_t)n); *size += (unsigned long long)n; }
	close(fd);
	sha_final(&s, hex);
	return n < 0;
}
/*
 * -e <path> (before the named files): that file's content is left out of the digest (its name
 * and size stay in). For the one file a running system does not read back as it is on the
 * disk: the libSystem.B.dylib in use reads with another SHA-256 than its blocks have, the
 * same other one on the M1 and in QEMU, on a read-only and on a writable root (the loader
 * changes its pages in the cache). Its hash is printed by naming it.
 */
static const char *except[8]; static int nexcept;
static int cmd_tree(const char *root, int nfiles, char **files)
{
	struct sha256 all, top; char hex[65], cur[256] = ""; int bad; double t0 = now();
	unsigned long long bytes = 0, counts[4] = { 0, 0, 0, 0 };
	{ struct stat st; if (stat(root, &st) == 0) root_dev = st.st_dev; }
	bad = walk(root, "");
	qsort(ents, nents, sizeof(*ents), cmp_ent);
	sha_init(&all); sha_init(&top);
	for (size_t i = 0; i <= nents; i++) {
		char first[256] = "", full[2048], line[2400]; unsigned long long size = 0; int n;
		if (i < nents) { snprintf(first, sizeof(first), "%s", ents[i].path); char *slash = strchr(first, '/'); if (slash) *slash = 0; }
		if (i == nents || strcmp(first, cur) != 0) {
			if (cur[0]) { sha_final(&top, hex); printf("NVT_TREE_TOP %s %s\n", cur, hex); }
			if (i == nents) break;
			snprintf(cur, sizeof(cur), "%s", first); sha_init(&top);
		}
		snprintf(full, sizeof(full), "%s/%s", root, ents[i].path);
		n = snprintf(line, sizeof(line), "%c %s", ents[i].type, ents[i].path) + 1;
		if (ents[i].type == 'f') {
			int skip = 0;
			for (int e = 0; e < nexcept; e++) skip |= !strcmp(except[e], ents[i].path);
			if (skip) { struct stat st; snprintf(hex, sizeof(hex), "content-not-compared"); if (stat(full, &st) == 0) size = (unsigned long long)st.st_size; }
			else if (file_sha(full, hex, &size)) { printf("NVT_TREE_FAIL read %s: %s\n", full, strerror(errno)); bad = 1; snprintf(hex, sizeof(hex), "unreadable"); }
			n += snprintf(line + n, sizeof(line) - (size_t)n, "%llu %s", size, hex) + 1;
			bytes += size; counts[0]++;
		} else if (ents[i].type == 'l') {
			char target[1024]; ssize_t t = readlink(full, target, sizeof(target) - 1);
			if (t < 0) { t = 0; bad = 1; }
			target[t] = 0;
			n += snprintf(line + n, sizeof(line) - (size_t)n, "%s", target) + 1;
			counts[2]++;
		} else counts[ents[i].type == 'd' ? 1 : 3]++;
		sha_update(&all, (const uint8_t *)line, (size_t)n); sha_update(&top, (const uint8_t *)line, (size_t)n);
	}
	sha_final(&all, hex);
	printf("NVT_TREE_ALL files=%llu dirs=%llu links=%llu other=%llu bytes=%llu errors=%d %s seconds=%.1f\n", counts[0], counts[1], counts[2],
	    counts[3], bytes, bad, hex, now() - t0);
	for (int i = 0; i < nfiles; i++) {
		char full[2048]; unsigned long long size = 0;
		snprintf(full, sizeof(full), "%s/%s", root, files[i]);
		if (file_sha(full, hex, &size)) { printf("NVT_FILE_FAIL %s: %s\n", files[i], strerror(errno)); bad = 1; }
		else printf("NVT_FILE %s %llu %s\n", files[i], size, hex);
	}
	return bad;
}

#ifdef __APPLE__
#include <sys/sysctl.h>
/*
 * stopreport <nonce>: the end of a boot whose root filesystem is on the disk this driver
 * serves. After the controller stops nothing can be read from the root any more, so one
 * process that is already in memory does all of it: the report once as a rehearsal (every
 * page it needs is then resident), the stop, the report again, and the bench's end marker.
 */
static void report(const char *when)
{
	static const char *const names[] = { "kern.sf_nvme.state", "kern.sf_nvme.stats", "kern.sf_nvme.fence", "kern.sf_nvme.part", "kern.sf_nvme.media",
	    /* the write-fenced build only; the read-only driver has neither and the line then says so */
	    "kern.sf_nvme.wstats", "kern.sf_nvme.wgate_selftest" };
	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		char text[1200] = ""; size_t len = sizeof(text) - 1;
		if (sysctlbyname(names[i], text, &len, NULL, 0) != 0) snprintf(text, sizeof(text), "(%s)", strerror(errno));
		printf("NV_%s %s %s\n", when, names[i] + 13, text);
	}
	int v = -1; size_t l = sizeof(v);
	sysctlbyname("kern.sf_nvme.gate_rejected", &v, &l, NULL, 0); printf("NV_GATE_REJECTED %d\n", v);
	v = -1; l = sizeof(v);
	sysctlbyname("kern.sf_nvme.write_refused", &v, &l, NULL, 0); printf("NV_WRITE_REFUSED %d\n", v);
	fflush(stdout);
}
static int cmd_stopreport(const char *nonce)
{
	int one = 1, rc;
	char state[200] = ""; size_t len;
	setvbuf(stdout, NULL, _IONBF, 0);
	report("BEFORE_STOP");
	rc = sysctlbyname("kern.sf_nvme.stop", NULL, NULL, &one, sizeof(one));
	printf("NV_STOPCALL rc=%d %s\n", rc, rc ? strerror(errno) : "");
	for (int i = 0; i < 400; i++) {
		len = sizeof(state) - 1; state[0] = 0;
		sysctlbyname("kern.sf_nvme.state", state, &len, NULL, 0);
		if (!strncmp(state, "stopped", 7) || !strncmp(state, "failed", 6)) break;
		usleep(100000);
	}
	printf("NV_STATE %s\n", state);
	report("AFTER_STOP");
	printf("NV_END_%s\n\nPD_BENCH_DONE\n", nonce);
	return 0;
}
#endif

int main(int argc, char **argv)
{
#ifdef __APPLE__
	if (argc == 3 && !strcmp(argv[1], "stopreport"))
		return cmd_stopreport(argv[2]);
#endif
	if (argc >= 3 && !strcmp(argv[1], "treex")) {
		one_fs = 1;
		argv[1] = (char *)"tree";
	}
	if (argc >= 3 && !strcmp(argv[1], "tree")) {
		int at = 3;
		while (at + 1 < argc && !strcmp(argv[at], "-e") && nexcept < 8) { except[nexcept++] = argv[at + 1]; at += 2; }
		return cmd_tree(argv[2], argc - at, argv + at);
	}
	if (argc >= 3 && !strcmp(argv[1], "tree"))
		return cmd_tree(argv[2], argc - 3, argv + 3);
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc == 4 && !strcmp(argv[1], "hash"))
		return cmd_hash(argv[2], argv[3]);
	if (argc == 6 && !strcmp(argv[1], "seq"))
		return cmd_seq(argv[2], strtoull(argv[3], NULL, 0), strtoull(argv[4], NULL, 0), strtoull(argv[5], NULL, 0));
	if (argc == 5 && !strcmp(argv[1], "rand"))
		return cmd_rand(argv[2], (unsigned)strtoul(argv[3], NULL, 0), strtoull(argv[4], NULL, 0));
#ifdef __APPLE__
	if (argc == 3 && !strcmp(argv[1], "wtest"))
		return cmd_wtest(argv[2]);
#endif
	if (argc == 4 && !strcmp(argv[1], "head"))
		return cmd_head(argv[2], strtoull(argv[3], NULL, 0));
	fprintf(stderr, "usage: sfnvme-test hash|seq|rand|wtest|head ... (see the source)\n");
	return 2;
}
