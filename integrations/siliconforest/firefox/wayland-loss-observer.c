/* SPDX-License-Identifier: MIT
 * Diagnostic only: disconnect ONE admitted private-profile Firefox from its
 * compositor without restarting the shared desktop. No process is signalled.
 */
#include <crt_externs.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static char trigger[1024];

static double clock_seconds(void) {
    struct timespec value = {0};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return value.tv_sec + value.tv_nsec / 1e9;
}

static void *disconnect_when_triggered(void *unused) {
    (void)unused;
    double end = clock_seconds() + 90;
    struct stat status;
    while (clock_seconds() < end && stat(trigger, &status) != 0)
        usleep(50000);
    if (stat(trigger, &status) != 0) {
        fprintf(stderr, "SFFX_WAYLAND_LOSS pid=%d trigger-timeout=1 monotonic=%.6f\n", getpid(), clock_seconds());
        return NULL;
    }
    int match = -1, count = 0;
    for (int fd = 0; fd < 1024; ++fd) {
        struct sockaddr_un peer = {0};
        socklen_t size = sizeof(peer);
        if (getpeername(fd, (struct sockaddr *)&peer, &size) == 0 && peer.sun_family == AF_UNIX &&
            strnlen(peer.sun_path, sizeof(peer.sun_path)) < sizeof(peer.sun_path) &&
            (!strcmp(peer.sun_path, "/private/var/run/gpu-desktop/wayland-0") ||
             !strcmp(peer.sun_path, "/var/run/gpu-desktop/wayland-0"))) {
            match = fd;
            ++count;
        }
    }
    if (count != 1) {
        fprintf(stderr, "SFFX_WAYLAND_LOSS pid=%d refused=1 matches=%d monotonic=%.6f\n", getpid(), count, clock_seconds());
        return NULL;
    }
    errno = 0;
    int result = shutdown(match, SHUT_RDWR), error = errno;
    fprintf(stderr, "SFFX_WAYLAND_LOSS pid=%d fd=%d shutdown=%d errno=%d monotonic=%.6f\n",
            getpid(), match, result, error, clock_seconds());
    return NULL;
}

__attribute__((constructor)) static void arm_private_firefox(void) {
    const char *enabled = getenv("SFFX_TEST_WAYLAND_LOSS");
    const char *program = getprogname();
    if (!enabled || strcmp(enabled, "1") || !program || strcmp(program, "firefox"))
        return;
    int argc = *_NSGetArgc(), isolated = 0;
    char **argv = *_NSGetArgv();
    const char *profile = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--no-remote")) isolated = 1;
        if (!strcmp(argv[i], "--profile") && i+1 < argc) profile = argv[i+1];
    }
    const char *prefix = "/private/var/db/sffx/";
    if (!isolated || !profile || strncmp(profile, prefix, strlen(prefix)) || strstr(profile, "/../"))
        return;
    size_t length = strlen(profile);
    if (length < 8 || strcmp(profile+length-8, "/profile") || length+40 >= sizeof(trigger))
        return;
    snprintf(trigger, sizeof(trigger), "%.*s/wayland-disconnect.trigger", (int)(length-8), profile);
    pthread_t thread;
    int result = pthread_create(&thread, NULL, disconnect_when_triggered, NULL);
    if (!result) pthread_detach(thread);
    fprintf(stderr, "SFFX_WAYLAND_LOSS pid=%d armed=%d trigger=%s monotonic=%.6f\n",
            getpid(), result == 0, trigger, clock_seconds());
}
