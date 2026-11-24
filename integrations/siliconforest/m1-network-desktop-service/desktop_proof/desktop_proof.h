#ifndef DNDS_DESKTOP_PROOF_H
#define DNDS_DESKTOP_PROOF_H
#include <stddef.h>
#include <stdint.h>
struct dnds_desktop_facts { int32_t app_pid[2], shell_pid[2], client_pid[2]; };
/* Closed-record proof only. Caller establishes frontend wait(0), both pipe
 * EOFs, writer quiescence, and checks its observer's sticky error afterward.
 * Returns zero on complete local proof; facts are unchanged on every failure.
 * Serialize; valid nonce[49]/facts/callback views must be disjoint. Callback
 * bytes are borrowed, immutable, and valid only during the call. See README. */
int dnds_verify_closed(const char nonce[49], int32_t frontend_pid,
    void (*observe)(const char *, const uint8_t *, size_t, void *),
    void *ctx, struct dnds_desktop_facts *facts);
#endif
