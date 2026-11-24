/* Original bounded trackpad-enumeration diagnostic reader. License pending. */
#ifndef TPEC_H
#define TPEC_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define TPEC_DIAG_BYTES 556u
#define TPEC_MAX_RAW 4096u
enum tpec_exit { TPEC_EXIT_CAPTURED=0, TPEC_EXIT_FAIL=1, TPEC_EXIT_USAGE=2, TPEC_EXIT_REFUSED=77 };
enum tpec_copy { TPEC_PROVIDER=0, TPEC_INSTANCE=1 };
enum tpec_get { TPEC_GET_OK=0, TPEC_GET_NO_ENTRY=1, TPEC_GET_NO_PROPERTY=2, TPEC_GET_ERROR=-1 };
typedef int (*tpec_getter)(void *, enum tpec_copy, uint8_t *, size_t, size_t *, int32_t *);
struct tpec_summary {
 uint32_t stage, request_mask, metadata_payload_bytes, descriptor_payload_bytes, descriptor_bytes;
 int32_t result;
};
int tpec_nonce_valid(const char *);
int tpec_validate(const uint8_t *, size_t, struct tpec_summary *);
int tpec_run(int, char **, tpec_getter, void *, FILE *);
#endif
