# Two sequential owned IPv4 utuns, source001

This helper is a prerequisite inside the real Darwin DNS/HTTP socket client,
not a standalone network-success test. It creates one owned point-to-point utun
per phase: first peer10.0.2.3 for DNS, then the caller-validated public A for HTTP.
Both use local10.0.2.15/32 and MTU1500. It never retargets a live interface or
issues RTM_ADD/DELETE. Original code, license selection pending.

Public API is `owned_interfaces.h`. The caller establishes exclusive guest
network management, keeps the session single-threaded, and uses this pattern:

```c
struct dle_interfaces interfaces;
int ok = dle_interfaces_init(&interfaces, 1, record_raw, context) == 0;
if (ok) ok = dle_interfaces_open(&interfaces, DLE_DNS_IPV4) == 0;
/* Only now borrow interfaces.fd/name/index for the packet pump. Use a real
 * Darwin UDP socket, collect and validate DNS, and close all client sockets. */
if (dle_interfaces_close(&interfaces)) ok = 0;
if (ok) ok = dle_interfaces_open(&interfaces, validated_public_A) == 0;
/* Only now use a fresh real Darwin TCP socket. Caller enforces DNS lease,
 * destination/Host binding, actual socket I/O and strict HTTP validation. */
if (dle_interfaces_close(&interfaces)) ok = 0;
ok = ok && interfaces.phase_count == 2 && interfaces.cleanup_complete
        && !interfaces.failed;
```

Always close on error. The public fd remains-1 until all open checks pass; an
open failure closes the pending internal utun best-effort. Never close or dup a
borrowed fd. Close application sockets and join workers before closing the
phase. Return0 from cleanup alone is not DNS, TCP, HTTP or external-network
evidence. Phase count increments only after verified open. Errors remain sticky,
block phase2, and survive successful later best-effort cleanup.

The helper captures a bounded sorted inventory of all interface names/indices,
requires at most64 entries with no duplicates, and checks exact equality before
phase2 and after every close. A new utun must have name/index absent from the
initial inventory. Names/indices may be reused between phases only after the
old interface disappeared. `_baseline_count` and `_baseline` may be read for
receipt serialization; all underscored fields must remain unmodified.

Before allocation, query-only RTM_GET must return correlated ESRCH for both
local and peer. This intentionally refuses even broader pre-existing routes.
The interface is allocated through utun's unit0 control connection, configured
with SIOCAIFADDR and SIOCSIFMTU, then read back using name/index and
SIOCGIFFLAGS/ADDR/DSTADDR/NETMASK/MTU. The peer's actual route must be an exact
UP|HOST|DONE route on this index, gateway AF_INET local10.0.2.15, absent or /32
netmask, with no flags except optional IFREF. Local-route absence is rechecked.
Unknown flags or socket-address forms fail closed; the existing route codec is not
relaxed. No SIOCSIFDSTADDR, explicit route mutations, resolver changes, default
route, host-controller route or firewall changes are made.

Kernel provenance is the exact e7aa00a60f0b56ef35aa24252e81467787bf71ea XNU
files bound by source-validation.json. `in_ifinit` creates a HOST route;
`rtinit_locked` supplies `ifa_addr` (the local IPv4 address) as gateway.
Utun installs input/pre_output only in its protocol attachment, no ARP ioctl
initializer; its ioctl returns EOPNOTSUPP for address setup, which in_ifinit
accepts. This gives the supported AF_INET direct-peer route shape. A synthetic
wire fixture verifies that the existing root decoder accepts that shape and
still rejects an AF_LINK gateway. Actual target readback remains required.

Cleanup closes the owned control fd exactly once, then observes baseline
inventory equality and correlated local/peer ESRCH. It never repairs a mismatch
with raw route deletion. Close errors set descriptor uncertainty, making
cleanup_complete remain0. Successful eventual observations can set
cleanup_complete despite an earlier non-close failure, but return remains-1.
The kernel's interface-address scrub checks rt_ifa identity and static-route
protection under rnh_lock; this avoids the held raw mutator's GET→DELETE race.
Initial route creation can still replace existing clones/ARP entries, and
in_ifinit hides EEXIST. The exclusive fresh guest and preflight conflict contract
is therefore mandatory, not proven merely by passing integer1 to init.

The cleanup observation loop has64 attempts,25ms pauses and a6000ms monotonic
deadline checked between queries. Each query dependency has its own1500ms
deadline; one pending query and a caller callback can overrun the outer deadline.
Kernel socket/ioctl/close calls have no user-space bus-hang guarantee. The root
controller's existing overall deadline/recovery remains necessary. Unknown
inventory/route state is failure, never successful cleanup.

The optional record callback receives `get_request`/`get_response`, raw AF_ROUTE
bytes and exact length; it returns void. It must be bounded, non-reentrant, and
must not mutate/close this session or invoke other route queries. The root
consumer bounds its accepted record size and treats log failure as an overall
attempt failure; callback success is not inferred by this helper. There is no
printf or unbounded logging inside the helper. All temporary metadata sockets
are CLOEXEC and closed internally; utun is CLOEXEC/nonblocking when exposed.

Link only interface_state.c, interface_darwin.c, the frozen egress validator
(eg_ipv4_global), and root route/query_routes.c, route_wire.c, route_abi.c.
**Never include owned_routes.c or define its analysis-only override.**
The root query files are dependencies, not duplicated or modified here.

`python3 -B integrations/puredarwin/m1-network-egress-guest/interfaces/test_interfaces.py`
runs35 synthetic lifecycle/ownership/fault cases under ASan/UBSan and NDEBUG,
including every abstract open-step failure, partial create, unexpected route
fields, preserved sticky failure, delayed disappearance, changed baseline,
residual routes, close uncertainty, and invalid phase/peer. It compiles the real
adapter and query dependencies against the pinned Darwin SDK and verifies the
held mutator's default compile refusal. Host mocks test lifecycle decisions;
they do not execute native syscalls. No target execution is claimed.
