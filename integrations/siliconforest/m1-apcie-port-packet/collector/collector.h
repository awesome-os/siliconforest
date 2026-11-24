/* Original bounded userspace observation collector. License pending. */
#ifndef DL_APCIE_COLLECTOR_H
#define DL_APCIE_COLLECTOR_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define APC_BYTES 84u
#define APC_CAP 4096u
#define APC_PROPERTY "PDApcieBridgeObservationV1"
enum apc_get { APC_GET_OK = 0, APC_GET_MISSING = 1, APC_GET_RPC = -1 };
struct apc_summary {
  uint32_t reads, stage, valid, region, offset;
  int32_t result;
};
/* Exactly one bounded raw copy. No retry, userclient, mapping or hardware API.
 * On success len is actual copied bytes; detail is raw RPC status. cleanup is
 * first owned-port release error, independent of copied bytes. */
typedef int (*apc_getter)(void *, uint8_t *, size_t, size_t *, int32_t *,
                          int32_t *);
int apc_nonce_valid(const char *);
/* -1 malformed; otherwise source-consistent and out filled. Never mutates out
 * on malformed input. Invalid/unread register fields must be zero, following
 * the actual zero-initialized kernel adapter, whose failed read never stores.
 */
int apc_validate(const uint8_t *, size_t, struct apc_summary *);
/* Fixed CLI --nonce lowerhex48. Exit0 numericOBSERVED,77 gated,1error,2usage.
 */
int apc_run(int, char **, apc_getter, void *, FILE *);
#endif
