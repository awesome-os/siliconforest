/* Original passive T2 policy. License pending; no device operations. */
#ifndef M1TP_CHILD_POLICY_H
#define M1TP_CHILD_POLICY_H
#include <stddef.h>
#include <stdint.h>
#include "native_protocol/dl_spi_hid.h"
#include "child_state.h"
#ifdef __cplusplus
extern "C" {
#endif
/* 1: exact known input; 0: other envelope; -1: invalid input length/ID. */
int m1tp_child_input(const struct dlsp_message *message);
/* Identity only. The reference is never returned as an acquired descriptor. */
int m1tp_child_descriptor_equal(const uint8_t *actual, size_t actual_bytes,
                               const uint8_t *reference, size_t reference_bytes);
#ifdef __cplusplus
}
#endif
#endif
