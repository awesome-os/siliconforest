/* Original bounded passive observation policy. No upstream implementation copied. */
#ifndef M1TP_TRACE_H
#define M1TP_TRACE_H
#include <stddef.h>
#include <stdint.h>
#include "native_protocol/dl_spi_hid.h"
#ifdef __cplusplus
extern "C" {
#endif
#define M1TP_TRACE_MAX_RECORDS 20u
#define M1TP_TRACE_CHUNK_BYTES 64u
#define M1TP_TRACE_HEX_BYTES 129u
/* Serialized by the existing transport workloop. No new callback or owner. */
struct m1tp_trace_state {
    uint32_t seen02, seen3f, seen44;
    uint32_t logged02, logged3f, logged44, sequence;
    uint8_t announced, motion_logged;
};
struct m1tp_trace_record {
    uint32_t sequence, reason, seen02, seen3f, seen44;
    uint16_t bytes, crc16;
    uint8_t report_id;
};
/* Silent initialization; only the admitted active T2 poll may announce. */
void m1tp_trace_init(struct m1tp_trace_state *);
/* Returns1 exactly once. Does not emit I/O. */
int m1tp_trace_announce(struct m1tp_trace_state *);
/* Called only on an already CRC-validated dlsp message. Rechecks dev2 Input
 * identity/exact wire lengths. Ordinary02 captures include X/Y=-128; only
 * reserved motion selection requires both axes in descriptor range.
 * Counts saturate at UINT32_MAX. No payload pointer is retained. Returns1
 * for selection; on0 the output record is unchanged. Never changes dispatch. */
int m1tp_trace_select(struct m1tp_trace_state *, const struct dlsp_message *,
                      struct m1tp_trace_record *);
/* index is a zero-based64-byte chunk index. Returns source byte count, or0
 * without changing output for invalid arguments. Fixed129 bytes incl NUL. */
size_t m1tp_trace_hex(const uint8_t *, size_t, size_t, char [M1TP_TRACE_HEX_BYTES]);
#ifdef __cplusplus
}
#endif
#endif
