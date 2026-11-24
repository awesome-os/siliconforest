// SPDX-License-Identifier: MIT
// sf-nokill.dylib: for robustness tests of Activity Monitor with random input (it runs as root and can quit any
// process): DYLD_INSERT_LIBRARIES=sf-nokill.dylib makes kill(), killpg() and proc_terminate() aimed at any
// process but the caller itself do nothing but log a line (SFNOKILL pid=... sig=...) and answer success. The
// application's own signals still work.
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int proc_terminate(pid_t pid, int *sig);

#define INTERPOSE(replacement, original) \
    __attribute__((used)) static const struct { const void *r; const void *o; } interpose_##original \
    __attribute__((section("__DATA,__interpose"))) = { (const void *)replacement, (const void *)original }

static int my_kill(pid_t pid, int sig) {
    if (pid == getpid() || pid == 0 || sig == 0)
        return kill(pid, sig);
    fprintf(stderr, "SFNOKILL kill pid=%d sig=%d (not sent: robustness test)\n", (int)pid, sig);
    return 0;
}

static int my_killpg(pid_t pgrp, int sig) {
    if (pgrp == getpgrp() || sig == 0)
        return killpg(pgrp, sig);
    fprintf(stderr, "SFNOKILL killpg pgrp=%d sig=%d (not sent: robustness test)\n", (int)pgrp, sig);
    return 0;
}

static int my_proc_terminate(pid_t pid, int *sig) {
    fprintf(stderr, "SFNOKILL proc_terminate pid=%d (not sent: robustness test)\n", (int)pid);
    if (sig) *sig = SIGTERM;
    return 0;
}

INTERPOSE(my_kill, kill);
INTERPOSE(my_killpg, killpg);
INTERPOSE(my_proc_terminate, proc_terminate);
