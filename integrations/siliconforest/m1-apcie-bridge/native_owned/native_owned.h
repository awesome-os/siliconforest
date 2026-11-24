/* Native facade: no effect backend or public configuration entry in Step1. */
#ifndef BCM_OWNED_NATIVE_H
#define BCM_OWNED_NATIVE_H
#include "owned_types.h"
#ifdef __cplusplus
extern "C" {
#endif
int bno_native_acquire(const struct bno_request *, uint64_t, uint32_t, struct bno_token *);
int bno_native_zero(const struct bno_token *);
int bno_native_copy(const struct bno_token *, uint64_t, const void *, size_t);
int bno_native_retire(const struct bno_token *);
int bno_native_inspect(uint32_t, struct bno_snapshot *);
int bno_native_run(const struct bno_token *, const struct bno_effect_request *, struct bno_effect_result *);
#ifdef __cplusplus
}
#endif
#endif
