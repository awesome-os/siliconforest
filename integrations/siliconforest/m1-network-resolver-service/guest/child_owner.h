#ifndef DLR_CHILD_OWNER_H
#define DLR_CHILD_OWNER_H
#include <stddef.h>
#include <stdint.h>
#include "darwin_spawn.h"
#define DLR_CHILD_OUT 8192u
#define DLR_CHILD_ERR 65536u
enum dlr_child_kind { DLR_DAEMON=1,DLR_PROBE=2 };
struct dlr_child {
 enum dlr_child_kind kind;
 int initialized,created,reaped,lost,error,wait_status;
 int output_fd,error_fd,output_eof,error_eof;
 int term_sent,kill_sent,stop_requested;
 int32_t pid;
 uint64_t started,stop_at,last;
 size_t output_used,error_used;
 uint8_t output[DLR_CHILD_OUT],errors[DLR_CHILD_ERR];
 struct chonk_spawn_result_v1 spawn;
};
/* Zeroed fresh caller-owned state. Sole PID owner; foreground/no-descendant
 * executables only. No kill-by-name, shell/PATH, process-group or PID reuse.
 * Tick drains at most 8KiB per pipe and reaps WNOHANG. On ECHILD ownership is
 * lost and permanently prohibits signaling. Error remains sticky. */
int dlr_child_start(struct dlr_child *,enum dlr_child_kind,const char nonce[49],uint64_t now);
void dlr_child_stop(struct dlr_child *,uint64_t now);
void dlr_child_tick(struct dlr_child *,uint64_t now);
int dlr_child_done(const struct dlr_child *);
#endif
