/* Original pure session ordering model. No syscalls, descriptors, or signals. */
#ifndef DNDS_COORDINATOR_H
#define DNDS_COORDINATOR_H
#include <stdint.h>
#define DNDS_WHOLE_MS UINT64_C(180000)
#define DNDS_SETUP_MS UINT64_C(20000)
#define DNDS_DESKTOP_MS UINT64_C(120000)
#define DNDS_FRONTEND_DRAIN_MS UINT64_C(10000)
#define DNDS_DAEMON_DRAIN_MS UINT64_C(5000)
#define DNDS_LINK_DRAIN_MS UINT64_C(15000)

enum dnds_phase { DNDS_PREFLIGHT,DNDS_ROUTE_WAIT,DNDS_DAEMON_WAIT,
 DNDS_FRONTEND_WAIT,DNDS_ACTIVE,DNDS_FRONTEND_DRAIN,DNDS_DAEMON_DRAIN,
 DNDS_LINK_DRAIN,DNDS_ROUTE_DRAIN,DNDS_SEALED };
enum dnds_kind { DNDS_TICK,DNDS_PREFLIGHT_OK,DNDS_ROUTE_READY,
 DNDS_DAEMON_CREATED,DNDS_DAEMON_READY,DNDS_FRONTEND_CREATED,
 DNDS_FRONTEND_CLOSED,DNDS_DAEMON_CLOSED,DNDS_LINK_CLOSED,
 DNDS_ROUTE_CLOSED,DNDS_FAILURE };
enum dnds_action { DNDS_OPEN_ROUTE=1,DNDS_START_DAEMON=2,
 DNDS_START_FRONTEND=4,DNDS_STOP_FRONTEND=8,DNDS_STOP_DAEMON=16,
 DNDS_CLOSE_LINK=32,DNDS_CLOSE_ROUTE=64,DNDS_SEAL=128 };
enum dnds_error { DNDS_BAD_EVENT=1,DNDS_SETUP_TIMEOUT=2,
 DNDS_DRAIN_TIMEOUT=4,DNDS_WHOLE_TIMEOUT=8,DNDS_EXTERNAL_FAILURE=16,
 DNDS_CLOCK_ERROR=32,DNDS_BAD_PROOF=64,DNDS_SPAWN_ERROR=128 };
/* Adapter must independently validate each complete proof before setting bits.
 * This model cannot establish provenance by receiving an integer mask. */
#define DNDS_PREFLIGHT_PROOF UINT32_C(0x0000000f) /* context/config/paths/default absence */
#define DNDS_ROUTE_PROOF UINT32_C(0x0000000f) /* ownership/ADD ACK/GET/baseline */
#define DNDS_DAEMON_READY_PROOF UINT32_C(0x00000007) /* owned UDS/PID/config */
#define DNDS_FRONTEND_PROOF UINT32_C(0x0000ffff)
/* frontend proof bits: wait0; stdout EOF; stderr EOF; restored signals;
 * session seal; app1/2 READY; app1/2 complete CLEANUP; both app reaps+status EOF;
 * both PTY actual waits; both initial DS4; both client network records;
 * both later FD checks; both real fallback prompts+foreground; synced records.
 * Last conjuncts each require both distinct instance identities, not counts. */
#define DNDS_DAEMON_CLOSED_PROOF UINT32_C(0x0000003f) /* wait4/TERM/noKILL/2 EOF/identities */
#define DNDS_LINK_PROOF UINT32_C(0x0000000f) /* both CLOSE/zero pending/full ACK/EOF */
#define DNDS_ROUTE_CLOSED_PROOF UINT32_C(0x00000007) /* close/interface absent/full baseline */
struct dnds_event { enum dnds_kind kind; int32_t pid; int32_t wait_status;
 uint32_t proof; uint32_t local_spawn_error; };
struct dnds_state { enum dnds_phase phase; uint32_t errors;
 uint64_t started,last,whole_deadline,phase_deadline;
 int32_t daemon_pid,frontend_pid,daemon_wait_status,frontend_wait_status;
 uint8_t daemon_reaped,frontend_reaped,complete; };
/* Positive PID ownership belongs to the adapter's real child_owner, BEFORE it
 * calls this model, even on local spawn cleanup error or a rejected event.
 * This module only retains observed identities; never sends signals or forgets
 * a real child. Duplicate/new unexpected PIDs remain owned by the adapter.
 * Each action is a request, never a completion or reap acknowledgement. */
int dnds_init(struct dnds_state *,uint64_t started);
uint32_t dnds_step(struct dnds_state *,uint64_t now,const struct dnds_event *);
#endif
