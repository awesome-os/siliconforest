/* SPDX-License-Identifier: MIT
 * Windowless bundle fixture: record actual exec arguments, never open a URL.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void string(FILE *out, const char *value) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p == '"' || *p == '\\') fputc('\\', out);
        if (*p < 32) fprintf(out, "\\u%04x", *p);
        else fputc(*p, out);
    }
    fputc('"', out);
}

int main(int argc, char **argv) {
    const char *path = getenv("SFFX_ARGV_OUTPUT");
    if (!path) return 2;
    FILE *out = fopen(path, "w");
    if (!out) return 2;
    fprintf(out, "{\"pid\":%ld,\"argv\":[", (long)getpid());
    for (int i = 0; i < argc; i++) {
        if (i) fputc(',', out);
        string(out, argv[i]);
    }
    fputs("]}\n", out);
    return fclose(out) == 0 ? 0 : 2;
}
