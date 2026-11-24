/* SPDX-License-Identifier: MIT
 * Native CLI observer admission control: one own socket, no Wayland messages,
 * no windows, GPU access, input, process signal or compositor restart.
 */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *profile = NULL;
    int expect = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--profile") && i+1 < argc) profile = argv[i+1];
        if (!strcmp(argv[i], "--expect-disconnect")) expect = 1;
    }
    if (!profile || strncmp(profile, "/private/var/db/sffx/", 21) || strstr(profile, "/../")) return 2;
    size_t length = strlen(profile);
    if (length < 8 || strcmp(profile+length-8, "/profile") || length > 900) return 2;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un peer = {0};
    peer.sun_family = AF_UNIX;
    strcpy(peer.sun_path, "/private/var/run/gpu-desktop/wayland-0");
    if (fd < 0 || connect(fd, (struct sockaddr *)&peer, sizeof(peer)) != 0) return 3;
    char trigger[1024];
    snprintf(trigger, sizeof(trigger), "%.*s/wayland-disconnect.trigger", (int)(length-8), profile);
    FILE *file = fopen(trigger, "wx");
    if (!file) return 4;
    fputs("Only disconnect this native control socket\n", file);
    fclose(file);
    struct pollfd waiting = {fd, POLLIN, 0};
    int polled = poll(&waiting, 1, expect ? 3000 : 500);
    char byte;
    ssize_t received = polled > 0 ? recv(fd, &byte, 1, MSG_DONTWAIT) : -1;
    int passed = expect ? polled > 0 && received == 0 : polled == 0;
    printf("SFFX_LOSS_CONTROL expect=%d poll=%d events=%x received=%ld errno=%d pass=%d\n",
           expect, polled, waiting.revents, (long)received, errno, passed);
    close(fd);
    return passed ? 0 : 1;
}
