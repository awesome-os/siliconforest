/* Original SHA-256 for the diagnostic receipt. License pending. */
#ifndef DLS_COLLECT_SHA256_H
#define DLS_COLLECT_SHA256_H
#include <stddef.h>
#include <stdint.h>
void dls_sha256(const uint8_t *data, size_t length, uint8_t out[32]);
#endif
