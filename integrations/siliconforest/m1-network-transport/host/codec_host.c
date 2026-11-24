/* Original Linux-host FFI allocation shim. No opaque C layout guesses. */
#include "../codec/transport.h"
#include <stdlib.h>
struct pair { struct dln_tx tx; struct dln_rx rx; };
void *dln_host_create(const char *hex, size_t n, uint8_t tx_direction) {
    uint8_t nonce[DLNET_NONCE_BYTES];
    struct pair *p;
    if ((tx_direction != 1 && tx_direction != 2) || !dln_nonce_hex(hex, n, nonce))
        return NULL;
    p = malloc(sizeof *p);
    if (!p) return NULL;
    if (dln_tx_init(&p->tx, nonce, tx_direction) ||
        dln_rx_init(&p->rx, nonce, (uint8_t)(3 - tx_direction))) {
        free(p); return NULL;
    }
    return p;
}
void dln_host_destroy(void *context) { free(context); }
int dln_host_encode(void *context, uint8_t type, const uint8_t *data, size_t n,
                    uint8_t *out, size_t cap, size_t *written) {
    struct pair *p = context;
    return p ? dln_encode(&p->tx, type, data, n, out, cap, written) : DLN_ARGUMENT;
}
int dln_host_feed(void *context, const uint8_t *data, size_t n,
                  dln_receive receive, void *user) {
    struct pair *p = context;
    return p ? dln_feed(&p->rx, data, n, receive, user) : DLN_ARGUMENT;
}
int dln_host_end(void *context) {
    struct pair *p = context;
    return p ? dln_end(&p->rx) : DLN_ARGUMENT;
}
