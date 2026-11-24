#ifndef DLR_ENDPOINT_OWNER_H
#define DLR_ENDPOINT_OWNER_H
#include <stdint.h>
struct dlr_endpoint_identity { uint64_t device,inode;uint32_t mode,uid;int seen,removed; };
struct dlr_endpoints { int initialized,error,ready,cleaned;int32_t pid;struct dlr_endpoint_identity socket,pidfile; };
/* Trusted RAM directory, no preexisting endpoint/PID path. Config is staged,
 * exact, root-owned regular one-link and read-only to non-root. No config write.
 * Absence checks are meaningful only with caller's exclusive process context. */
int dlr_endpoints_preflight(struct dlr_endpoints *);
int dlr_endpoints_ready(struct dlr_endpoints *,int32_t owned_daemon_pid);
/* Only after sole daemon PID is reaped. Unlink only exact previously observed
 * inode+device+owner+mode, PID file additionally must name that same owned PID.
 * Foreign/replaced/unobserved leftovers are left untouched and disqualify. */
int dlr_endpoints_cleanup(struct dlr_endpoints *,int owned_daemon_reaped);
#endif
