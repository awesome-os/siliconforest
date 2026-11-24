/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <fcntl.h>
#include <sandbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    int fixture = open(argv[1], O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (fixture < 0) return 2;
    close(fixture);
    char *error = NULL;
    int invalid = sandbox_init("(version 1) (this-operation-does-not-exist)", 0, &error);
    printf("FXSANDBOX invalid_profile=%d error=%s\n", invalid, error ? error : "none");
    sandbox_free_error(error);
    if (invalid == 0) return 1;
    char *profile = NULL;
    /* The numeric expression is the first actual Firefox GPU-profile failure. */
    if (asprintf(&profile, "(version 1) (define number (string->number \"26\")) "
        "(define (forbid op) (deny op (literal \"%s\"))) "
        "(allow default) (if (> number 0) (forbid file-read-data))", argv[1]) < 0) return 2;
    error = NULL;
    int applied = sandbox_init(profile, 0, &error);
    printf("FXSANDBOX applied=%d error=%s\n", applied, error ? error : "none");
    free(profile);
    sandbox_free_error(error);
    if (applied != 0) return 1;
    errno = 0;
    int denied = open(argv[1], O_RDONLY), denied_errno = errno;
    int allowed = open("/dev/null", O_RDONLY);
    printf("FXSANDBOX denied_fd=%d errno=%d allowed_fd=%d\n", denied, denied_errno, allowed);
    if (denied >= 0) close(denied);
    if (allowed >= 0) close(allowed);
    return denied == -1 && (denied_errno == EPERM || denied_errno == EACCES) && allowed >= 0 ? 0 : 1;
}
