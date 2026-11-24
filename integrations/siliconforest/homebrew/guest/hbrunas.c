// SPDX-License-Identifier: MIT
// hb-runas UID GID COMMAND [ARGS...]: run a command as another user. The guest's serial shell
// is root and the image has no su or sudo; Homebrew refuses to run as root.
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: hb-runas uid gid command [args]\n");
        return 2;
    }
    gid_t gid = (gid_t)atoi(argv[2]);
    uid_t uid = (uid_t)atoi(argv[1]);
    if (setgroups(1, &gid) || setgid(gid) || setuid(uid)) {
        perror("hb-runas");
        return 1;
    }
    execvp(argv[3], argv + 3);
    perror(argv[3]);
    return 127;
}
