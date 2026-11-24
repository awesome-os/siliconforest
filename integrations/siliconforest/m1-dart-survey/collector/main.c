#include "fetch.h"
#include "receipt.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *nonce = 0;
    uint8_t buf[DLS_SNAPSHOT_BYTES];
    size_t n = 0;
    int instance = DLS_INSTANCE_UNREAD;
    int fetch_rc;
    if (dls_take_nonce(argc, argv, &nonce) != 0) {
        fprintf(stderr, "usage: darwin-dart-survey --nonce HEX48\n");
        return DLS_EXIT_USAGE;
    }
    memset(buf, 0, sizeof(buf));
    fetch_rc = dls_fetch_diagnostic(buf, sizeof(buf), &n, &instance);
    return dls_emit(stdout, nonce, buf, n, fetch_rc, instance);
}
