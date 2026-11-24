#ifndef DLR_PROBE_RECORD_H
#define DLR_PROBE_RECORD_H
#include <stddef.h>
#include <stdint.h>
#define DLR_PROBE_CAP 8192u
#define DLR_ADDRESS_CAP 32u
struct dlr_probe_record { int32_t pid; unsigned count; uint32_t ipv4[DLR_ADDRESS_CAP]; };
/* Strict successful serializer-v1 grammar, complete sole-writer line only.
 * Preserves API order/duplicates. No DNS wire-order inference or fallback. */
int dlr_probe_parse(const uint8_t *,size_t,const char nonce[49],int32_t expected_pid,struct dlr_probe_record *);
#endif
