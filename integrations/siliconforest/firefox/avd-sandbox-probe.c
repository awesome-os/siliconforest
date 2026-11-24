/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <fcntl.h>
#include <sandbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    int positive = !strcmp(argv[1], "allow");
    const char *profile = positive ?
        "(version 1)(deny default)(allow file-read-data (literal \"/dev/null\"))"
        "(allow iokit-open (iokit-connection \"IOAccelerator\"))" :
        "(version 1)(deny default)(allow file-read-data (literal \"/dev/null\"))"
        "(allow iokit-open (iokit-connection \"IOAudioEngine\"))";
    char *error = NULL;
    int status = sandbox_init(profile, 0, &error);
    printf("FXAVDSB positive=%d applied=%d error=%s\n", positive, status, error ? error : "none");
    sandbox_free_error(error);
    if (status) return 1;
    errno=0; int avd = open("/dev/sfavd", O_RDWR), avd_errno=errno;
    errno=0; int other = open("/dev/zero", O_RDONLY), other_errno=errno;
    int control = open("/dev/null", O_RDONLY);
    printf("FXAVDSB avd_fd=%d errno=%d unrelated_fd=%d errno=%d control_fd=%d\n",avd,avd_errno,other,other_errno,control);
    if(avd>=0)close(avd);if(other>=0)close(other);if(control>=0)close(control);
    int denied = avd<0 && (avd_errno==EPERM || avd_errno==EACCES);
    return (positive ? avd>=0 : denied) && other<0 && (other_errno==EPERM || other_errno==EACCES) && control>=0 ? 0 : 1;
}
