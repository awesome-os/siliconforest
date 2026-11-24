// SPDX-License-Identifier: MIT
// sf-dyld-probe: the dyld installer's proof (../guest/sf-dyld-install.py) of the optimised dyld
// (../../libsystem-default/build-libsystem-opt.nix with patches/dyld-libobjc-search-paths.patch).
// Built like sf-arm64e-probe (a plain arm64 program with the arm64e subtype in its header, so a dyld
// without patches/dyld-arm64e-keys-off.patch refuses it), and it also needs the new dyld's change:
// libobjc, which SiliconForest's dyld loads into every program itself, follows DYLD_LIBRARY_PATH.
// The probe copies /usr/lib/libobjc.A.dylib into a directory of its own, runs itself again with
// DYLD_LIBRARY_PATH naming it, and the second run says where its libobjc came from.
// Exits 0 when that is the copy, 1 when it is /usr/lib's (the dyld of before), 3 on a setup error.
#include <dlfcn.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    (void)argc;
    const char *child = getenv("SF_DYLD_PROBE_DIR");
    if (child != NULL) {
        Dl_info info;
        if (dladdr((const void *)&objc_getClass, &info) == 0 || info.dli_fname == NULL) return 3;
        int ours = strncmp(info.dli_fname, child, strlen(child)) == 0;
        printf("sf-dyld-probe: libobjc from %s (%s)\n", info.dli_fname,
               ours ? "DYLD_LIBRARY_PATH reaches libobjc" : "DYLD_LIBRARY_PATH does not reach libobjc");
        return ours ? 0 : 1;
    }
    char dir[64], lib[96], self[1024];
    uint32_t size = sizeof self;
    if (_NSGetExecutablePath(self, &size) != 0) return 3;
    snprintf(dir, sizeof dir, "/private/tmp/sf-dyld-probe.%d", (int)getpid());
    snprintf(lib, sizeof lib, "%s/libobjc.A.dylib", dir);
    if (mkdir(dir, 0700) != 0) return 3;
    int in = open("/usr/lib/libobjc.A.dylib", O_RDONLY), out = open(lib, O_WRONLY | O_CREAT | O_EXCL, 0755);
    if (in < 0 || out < 0) return 3;
    char buf[65536];
    ssize_t n;
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, (size_t)n) != n) return 3;
    close(in); close(out);
    pid_t pid = fork();
    if (pid == 0) {
        setenv("SF_DYLD_PROBE_DIR", dir, 1);
        setenv("DYLD_LIBRARY_PATH", dir, 1);
        execv(self, argv);
        _exit(3);
    }
    int st = 0;
    if (pid < 0 || waitpid(pid, &st, 0) != pid) st = 3 << 8;
    unlink(lib); rmdir(dir);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 3;
}
