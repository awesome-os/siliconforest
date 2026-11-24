/* Separate desktop-service adapter. Frozen resolver child_owner is unchanged. */
#ifndef DNDS_OWNED_CHILD_H
#define DNDS_OWNED_CHILD_H
#include <stddef.h>
#include <stdint.h>
#include "darwin_spawn.h"
#define DNDS_CHILD_OUT 65536u
#define DNDS_CHILD_ERR 65536u
enum dnds_child_kind { DNDS_MDNS=1, DNDS_FRONTEND=2 };
struct dnds_child {
 enum dnds_child_kind kind;
 int initialized,created,reaped,lost,error,wait_status;
 int output_fd,error_fd,output_eof,error_eof;
 int term_sent,kill_sent,stop_requested;
 int32_t pid;
 uint64_t started,stop_at,last,kill_after_ms;
 size_t output_used,error_used;
 uint8_t output[DNDS_CHILD_OUT],errors[DNDS_CHILD_ERR];
 struct chonk_spawn_result_v1 spawn;
};
/* Owns only the exact positive PID returned by the reviewed native shim.
 * The frontend owns all its descendants. Its successful reap alone is NOT a
 * descendant-cleanup proof; closed session/application records are separate.
 * A forced frontend KILL permanently fails that proof. Never kill by cached
 * name, descendant PID or process group from this adapter. */
int dnds_child_start(struct dnds_child *,enum dnds_child_kind,const char nonce[49],uint64_t);
void dnds_child_stop(struct dnds_child *,uint64_t);
void dnds_child_tick(struct dnds_child *,uint64_t);
int dnds_child_done(const struct dnds_child *);
#endif
