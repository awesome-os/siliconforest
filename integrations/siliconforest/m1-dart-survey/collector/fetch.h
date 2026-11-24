/* Target property fetch. The MIG body is fetch_mig.c. License pending. */
#ifndef DLS_COLLECT_FETCH_H
#define DLS_COLLECT_FETCH_H
#include <stddef.h>
#include <stdint.h>

enum {
    DLS_FETCH_OK = 0,
    DLS_FETCH_MISSING = 1,
    DLS_FETCH_MIG = 2,
    DLS_FETCH_LENGTH = 3,
    DLS_FETCH_INSTANCE = 4
};

enum {
    DLS_INSTANCE_UNREAD = -1,
    DLS_INSTANCE_ABSENT = 0,
    DLS_INSTANCE_EQUAL = 1
};

/* One persistent read, then at most one instance read. No retry.
 * On OK, *out_n is 764 and *instance is absent or equal.
 * On LENGTH, *out_n is the short count and dst holds those bytes.
 * On INSTANCE, dst holds the 764 persistent bytes.
 * On MISSING or MIG, *out_n is 0. */
int dls_fetch_diagnostic(uint8_t *dst, size_t cap, size_t *out_n, int *instance);
#endif
