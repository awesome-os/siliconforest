/* Original darwin-lab read-only survey collector core. License pending. */
#ifndef DL_DART_COLLECTOR_H
#define DL_DART_COLLECTOR_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define DLC_DIAG_BYTES 764u   /* PDDARTSurveyDiagnosticV1, observation_layout.h */
#define DLC_MAX_RAW 4096u     /* io_struct_inband_t; larger properties are a MIG error */

enum dlc_exit { DLC_EXIT_SAMPLED = 0, DLC_EXIT_FAIL = 1, DLC_EXIT_USAGE = 2, DLC_EXIT_REFUSED = 77 };
enum dlc_copy { DLC_PROVIDER = 0, DLC_INSTANCE = 1 };
enum dlc_get { DLC_GET_OK = 0, DLC_GET_NO_ENTRY = 1, DLC_GET_NO_PROPERTY = 2, DLC_GET_ERROR = -1 };

/* Read one registry property's raw bytes exactly once into buf (cap bytes).
 * Returns DLC_GET_*; *len is set only on DLC_GET_OK, *detail carries the raw
 * kern_return_t (0 if none). Implementations must not retry, open a user
 * client, map memory or touch hardware.
 */
typedef int (*dlc_getter)(void *ctx, enum dlc_copy which, uint8_t *buf, size_t cap,
                          size_t *len, int32_t *detail);

struct dlc_summary { uint32_t sample_count, reads, region, offset; int32_t result; };

int dlc_nonce_valid(const char *nonce);
/* 0 and *out filled when the bytes are a well-formed V1 diagnostic consistent
 * with the reviewed sampler; -1 otherwise (out untouched). */
int dlc_validate(const uint8_t *bytes, size_t length, struct dlc_summary *out);
/* Whole CLI: darwin-dart-survey --nonce HEX48. Returns the process exit code. */
int dlc_run(int argc, char **argv, dlc_getter get, void *ctx, FILE *out);
#endif
