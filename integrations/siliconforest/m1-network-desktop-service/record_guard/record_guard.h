/* Original read-only record boundary. License selection pending. */
#ifndef RECORD_GUARD_H
#define RECORD_GUARD_H
#include <stddef.h>
#include <stdint.h>
#define RG_MAX_BYTES 262144u
#define RG_MAX_TOKENS 4096u
#define RG_MAX_DEPTH 32u
#define RG_MAX_STEPS 4194304u
enum rg_code { RG_OK, RG_ARGUMENT, RG_PATH, RG_IO, RG_METADATA, RG_CHANGED,
    RG_LIMIT, RG_JSON, RG_DUPLICATE, RG_TYPE, RG_MISSING, RG_RANGE };
struct rg_time { int64_t seconds; uint32_t nanoseconds; };
struct rg_identity { uint64_t device, inode; };
struct rg_snapshot {
    const uint8_t *bytes; size_t length;
    struct rg_identity directory, file;
    struct rg_time mtime, ctime;
};
/* Heap/static caller storage, not a large automatic stack object. */
struct rg_read_workspace { uint8_t bytes[RG_MAX_BYTES]; };
/* Full session path is exactly /private/var/run/darwin-desktop-network/session-
 * plus the same lowercase hex48 nonce. Slot0 reads session directory, 1/2 derive
 * instance0001/0002 + nonce[:44]. Basename is bounded single component.
 * Named caps: result.json/network-client.json16KiB, network-client.pid32,
 * network-shell-return.txt512; other admitted basenames <=256KiB.
 * Caller must already establish writer/process quiescence. Stat checks detect
 * some races; they do not prove a file is closed. No retries after uncertainty.
 * Output unchanged on failure; scratch unspecified. Snapshot borrows scratch. */
enum rg_code rg_read(const char *session, const char *nonce, unsigned slot,
    const char *basename, struct rg_read_workspace *, struct rg_snapshot *);
enum rg_json_type { RG_OBJECT, RG_ARRAY, RG_STRING, RG_INTEGER, RG_BOOL, RG_NULL };
struct rg_token {
    enum rg_json_type type; uint32_t start, length, next, children;
    uint64_t magnitude; unsigned negative;
};
struct rg_json_workspace { struct rg_token tokens[RG_MAX_TOKENS]; };
struct rg_document { const uint8_t *bytes; size_t length; const struct rg_token *tokens; size_t count; };
enum rg_step_kind { RG_KEY, RG_INDEX };
struct rg_step { enum rg_step_kind kind; const char *key; size_t length, index; };
/* Strict ASCII integer JSON: full int64-negative / uint64-positive magnitude,
 * no floating/exponent values. Unknown keys accepted; duplicate decoded keys
 * anywhere fail. Escaped ASCII controls (including NUL) are length-preserving;
 * non-ASCII raw/escaped values and surrogate escapes fail. No locale parsing.
 * Document borrows unchanged bytes/tokens. Output unchanged on failure;
 * workspace scratch unspecified. Caller serializes and preserves both lifetimes. */
enum rg_code rg_json_parse(const uint8_t *, size_t, struct rg_json_workspace *, struct rg_document *);
enum rg_code rg_json_path(const struct rg_document *, const struct rg_step *, size_t, uint32_t *);
enum rg_code rg_json_u64(const struct rg_document *, uint32_t, uint64_t *);
enum rg_code rg_json_i64(const struct rg_document *, uint32_t, int64_t *);
enum rg_code rg_json_bool(const struct rg_document *, uint32_t, int *);
enum rg_code rg_json_null(const struct rg_document *, uint32_t);
/* Length-delimited decoded bytes, with an additional terminating NUL. Embedded
 * NUL remains part of length: callers must compare length+bytes, not strlen. */
enum rg_code rg_json_string(const struct rg_document *, uint32_t, char *, size_t, size_t *);
#endif
