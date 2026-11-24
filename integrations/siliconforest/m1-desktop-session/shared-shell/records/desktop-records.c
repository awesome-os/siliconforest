#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
/* Installed name: darwin-desktop-records.
 * Read-only export of one private Shell diagnostics directory.
 * A live snapshot is a prefix. Closed requires a stable size.
 * This program makes no Shell, spawn, or input verdict.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

struct record { const char *name; size_t limit; };
static const struct record session_records[] = {
    {"session-result.txt", 16384},
    {"manager-status.log", 65536}
};
static const struct record application_records[] = {
    {"application-stdout.log", 8192},
    {"application-stderr.log", 8192}
};

static int nonce_valid(const char *n)
{
    if (strlen(n) != 48) return 0;
    for (size_t i = 0; i < 48; ++i)
        if (!((n[i] >= '0' && n[i] <= '9') || (n[i] >= 'a' && n[i] <= 'f'))) return 0;
    return 1;
}

static int directory_argument(const char *path)
{
    if (!path || path[0] != '/') return 0;
    const char *p = path;
    int saw = 0;
    while (*p) {
        while (*p == '/') ++p;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != '/') ++p;
        size_t n = (size_t)(p - start);
        if ((n == 1 && start[0] == '.') || (n == 2 && start[0] == '.' && start[1] == '.'))
            return 0;
        saw = 1;
    }
    return saw;
}

static int emit_record(int directory, const struct record *r, const char *nonce,
                       const char *kind, int closed)
{
    int fd = openat(directory, r->name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        printf("DL22_RECORD_MISSING %s %s %s errno=%d\n", nonce, kind, r->name, errno);
        return -1;
    }
    struct stat before, after;
    if (fstat(fd, &before) < 0 || !S_ISREG(before.st_mode) || before.st_nlink != 1 ||
        before.st_uid != geteuid() || (before.st_mode & 07777) != 0600 ||
        before.st_size < 0 || (unsigned long long)before.st_size > r->limit) {
        printf("DL22_RECORD_REFUSED %s %s %s\n", nonce, kind, r->name);
        if (close(fd) < 0) return -1;
        return -1;
    }
    size_t size = (size_t)before.st_size, offset = 0;
    unsigned char *buffer = malloc(size ? size : 1);
    if (!buffer) {
        close(fd);
        return -1;
    }
    while (offset < size) {
        ssize_t n = read(fd, buffer + offset, size - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            free(buffer);
            close(fd);
            return -1;
        }
        offset += (size_t)n;
    }
    if (fstat(fd, &after) < 0 || after.st_dev != before.st_dev || after.st_ino != before.st_ino ||
        after.st_size < before.st_size || (closed && after.st_size != before.st_size)) {
        free(buffer);
        close(fd);
        return -1;
    }
    if (close(fd) < 0) { free(buffer); return -1; }
    unsigned char digest[32];
#ifdef __APPLE__
    if (!CC_SHA256(buffer, (CC_LONG)size, digest)) { free(buffer); return -1; }
#else
    if (!SHA256(buffer, size, digest)) { free(buffer); return -1; }
#endif
    static const char hex[] = "0123456789abcdef";
    char digest_hex[65];
    for (size_t i = 0; i < 32; ++i) {
        digest_hex[2 * i] = hex[digest[i] >> 4];
        digest_hex[2 * i + 1] = hex[digest[i] & 15];
    }
    digest_hex[64] = '\0';
    printf("DL22_RECORD_BEGIN %s %s %s bytes=%zu chunk_bytes=64\n", nonce, kind, r->name, size);
    size_t chunks = 0;
    for (offset = 0; offset < size; offset += 64) {
        size_t count = size - offset < 64 ? size - offset : 64;
        char line[129];
        for (size_t i = 0; i < count; ++i) {
            line[2 * i] = hex[buffer[offset + i] >> 4];
            line[2 * i + 1] = hex[buffer[offset + i] & 15];
        }
        line[2 * count] = '\0';
        printf("DL22_RECORD_HEX %s %s %s index=%zu %s\n", nonce, kind, r->name, chunks++, line);
    }
    free(buffer);
    printf("DL22_RECORD_END %s %s %s bytes=%zu chunks=%zu file_bytes_after=%llu sha256=%s\n",
           nonce, kind, r->name, size, chunks, (unsigned long long)after.st_size, digest_hex);
    return ferror(stdout) ? -1 : 0;
}

int main(int argc, char **argv)
{
    if (argc != 9 || strcmp(argv[1], "--directory") || strcmp(argv[3], "--nonce") ||
        strcmp(argv[5], "--kind") || strcmp(argv[7], "--phase") ||
        !directory_argument(argv[2]) || !nonce_valid(argv[4]) ||
        (strcmp(argv[6], "session") && strcmp(argv[6], "application")) ||
        (strcmp(argv[8], "live") && strcmp(argv[8], "closed"))) return 2;
    const struct record *table = !strcmp(argv[6], "session") ? session_records : application_records;
    size_t count = 2;
    int directory = open(argv[2], O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (directory < 0) return 1;
    struct stat st;
    if (fstat(directory, &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & 07777) != 0700) {
        close(directory);
        return 1;
    }
    int failed = 0, closed = !strcmp(argv[8], "closed");
    for (size_t i = 0; i < count; ++i)
        if (emit_record(directory, &table[i], argv[4], argv[6], closed) < 0) failed = 1;
    if (close(directory) < 0) failed = 1;
    if (fflush(stdout) || ferror(stdout)) failed = 1;
    return failed ? 1 : 0;
}
