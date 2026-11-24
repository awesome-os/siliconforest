#ifndef DNDS_SESSION_TEXT_H
#define DNDS_SESSION_TEXT_H
#include <stddef.h>
#include <stdint.h>
struct dnds_app_summary {
 int32_t pid;
 uint32_t shell_wait;
 uint64_t created_ms,ready_ms,cleanup_ms,reaped_ms,status_bytes;
 unsigned shell_term,shell_kill;
};
struct dnds_session_summary {
 uint64_t elapsed_ms;
 unsigned deadline_reached;
 struct dnds_app_summary app[2];
};
/* Exact successful frontend004 session-result serializer, not a general log
 * parser. This is one predicate: actual frontend wait0/EOF, private closed file
 * identity, app result/PTY/client records and raw wire proof remain separate.
 * Strict lowercase nonce; <=16384 ASCII bytes; no output change on failure. */
int dnds_session_text(const uint8_t *,size_t,const char nonce[49],
                     struct dnds_session_summary *);
#endif
