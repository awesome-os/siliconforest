/* Internal trusted-provider bridge, not an interface for importing memory. */
#ifndef UATB_WRITER_INTERNAL_H
#define UATB_WRITER_INTERNAL_H
#include "builder.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Verifies exact token, state and fresh provider view against the frozen owner.
 * A successful return borrows that view only within the common serialization
 * scope. Never expose it to users or asynchronous consumers. */
int uatb_owned_view(const struct duat_context *, struct duat_token,
                    struct duat_view *, void **handle);
enum uatb_span_result { UATB_SPAN_READY, UATB_SPAN_BAD, UATB_SPAN_UNCERTAIN };
enum uatb_span_result uatb_write_view(const struct duat_context *, struct duat_token,
    size_t offset, const uint8_t *source, size_t length, struct duat_view *, void **);
enum uatb_write_result uatb_iokit_write(void *, const struct duat_context *,
    struct duat_token, size_t, const uint8_t *, size_t);
/* Binds the real writer only to the unchanged real IOBMD allocation provider. */
enum uatb_code uatb_iokit_init(struct uatb_context *);
#ifdef __cplusplus
}
#endif
#endif
