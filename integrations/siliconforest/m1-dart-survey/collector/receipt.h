/* Bounded receipt writer. License pending. */
#ifndef DLS_COLLECT_RECEIPT_H
#define DLS_COLLECT_RECEIPT_H
#include "classify.h"
#include "fetch.h"
#include <stdio.h>
int dls_emit(FILE *out, const char *nonce, const uint8_t *bytes, size_t length,
             int fetch_rc, int instance);
#endif
