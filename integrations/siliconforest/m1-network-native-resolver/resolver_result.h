#ifndef DL_NATIVE_RESOLVER_RESULT_H
#define DL_NATIVE_RESOLVER_RESULT_H
#include <stddef.h>
#include <stdint.h>
#define RR_ADDRESS_CAP 32u
#define RR_OUTPUT_CAP 8192u
enum rr_error { RR_NONE, RR_GAI, RR_EMPTY, RR_SHAPE, RR_CAP, RR_CYCLE,
                RR_UNEXPECTED_FAILURE_RESULT, RR_ERROR_COUNT };
struct rr_address { uint8_t bytes[4]; };
struct rr_result {
 int32_t pid,gai_rc,system_errno;
 enum rr_error error;
 size_t count;
 struct rr_address addresses[RR_ADDRESS_CAP];
};
int rr_nonce(const char *,size_t);
/* Pure Darwin sockaddr classifier. No DNS/network/libc resolver substitution. */
int rr_address(struct rr_result *,int32_t family,int32_t socktype,int32_t protocol,
               const uint8_t *,size_t);
/* Single bounded ASCII JSON+LF. The caller alone writes its parent-owned pipe.
 * FAILED may contain only a validated prefix; complete=false makes that explicit. */
int rr_format(const struct rr_result *,const char *nonce,char *,size_t,size_t *);
#endif
