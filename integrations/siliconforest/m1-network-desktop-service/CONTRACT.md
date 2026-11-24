# Owned desktop network session proposal 001

Source-only contract. No new terminal, frontend, host profile, backend, service,
route mutation or target execution is implemented or qualified by this leaf.
The existing qualified networking and desktop experiments remain immutable.

The next actual candidate combines an owned network lifetime with the real
shared Shell and two terminal windows. Ordinary PTY programs call libc
getaddrinfo and BSD sockets. The outer network owner performs no HTTP request on
their behalf. It alone owns the serial SPACE/LF credit bridge, one utun, its
interface-bound default and the mdnsd lifetime. Frontend/app evidence is required
before service teardown; a signal or successful kill call is never orphan proof.

## Concrete profile and budgets

Proposed new explicit profile: `desktop-native-resolver-two-http-v1`. It must be
admitted by a new isolated typed host mode, never an existing one-flow profile.
Owner CLI: `/usr/bin/darwin-desktop-network-session --nonce HEX48 --seconds 180`.
Host worker budget: 285 seconds (below its general 300-second cap), leaving
approximately 78 seconds for measured bootstrap plus margin; external root-owned
controller deadline and verified Linux recovery remain mandatory. Local syscall
completion is not guaranteed by polling deadlines.

Relative to owner start: network/default/daemon setup must finish by 20 seconds;
launch frontend only before that cutoff, for 120 seconds; allow its actual
10-second cleanup loop (source session_entry.rs), then at most 5 seconds for
owned daemon stop/drain/reap/identity cleanup; allow 15 seconds for credit-drained
CLOSE/EOF; reserve the remaining interval through second 180 for interface close,
complete baseline readback, record seal and owner return. Every phase is also
bounded by the same owner whole deadline; progress never refreshes it. Network
setup failure skips desktop launch. Failure initiates cleanup immediately while
pumping serial/stdio/waits; it cannot turn later cleanup into network success.

No network operations occur after daemon retirement. Keep utun/bridge alive
through all terminal/frontend cleanup and mdnsd's normal exit/goodbye traffic.

## Launch and startup feasibility

Use the unchanged qualified frontend004 CLI:

```
/usr/bin/chonkstep-darwin-desktop --run --socket ABS --records ABS --session HEX48 --terminal /usr/bin/chonk-desktop-network-terminal --seconds 120 --font-regular ABS --font-bold ABS --input pdhid --startup-terminals 2
```

The current app002 cannot run a startup command: it fixes `/bin/sh -i +m` and
`ENV=/dev/null`, ignores caller shell environment, and has no command/script CLI.
Therefore a future **separate** terminal profile binary is needed. No wrapper or
environment trick can make the frozen app perform this task. Its exact existing
`--socket/--instance/--records/--seconds` CLI, fd3 CHONKTERM/1 protocol, unique48
identity, private0700/0600 files, Wayland/XKB/grid/SSD and owned cleanup stay intact.
Require exact trusted records ancestry
`/private/var/run/darwin-desktop-network/session-HEX48/INSTANCE48`: bounded absolute
paths, checked owner and0700 directories, counters0001/0002 only, and
`instance[4:] == session_nonce[:44]`. Derive the full session nonce only from that
exact root basename; no environment/config/command-file fallback. This binds
evidence identities, not a secret or cryptographic authentication. Frontend004
already selects an absolute terminal binary and sends precisely that CLI; no frontend command expansion is required. The new variant is unqualified
until separately source/build/runtime reviewed. This proposal does not implement
it or alter `/usr/bin/chonk-terminal-darwin`.

A fixed source-bound PTY startup program `/usr/bin/darwin-desktop-net-client`
executes first instead of the initial shell. Arguments are only its validated
instance identity, private record directory and remaining finite lifetime; no
commands, shell text, URLs, addresses or configuration are accepted. It directly
runs one `getaddrinfo("example.com","80",AF_INET/SOCK_STREAM/AI_NUMERICSERV)`,
preserves the exact ordered list (cap32), uses only result[0] for one nonblocking
BSD TCP connection to port80, writes the same 186-byte fixed example.com GET
with the full session48 nonce, accepts at most2048 HTTP bytes and actual peer EOF,
records actual local/peer IP+port/PID and closes all owned sockets. Request TCP
MSS256 and read it back; do not assume returned packet sizes or segment packets
in a userland bridge. No fallback address, hostname or endpoint. Two startup
programs may finish in either order, share cached DNS or cause retries.

The program prints an early bounded plain terminal banner so window readiness
need not wait for DNS. Network result includes full session nonce, instance, slot1/2, PID, ordered API
addresses and full local/remote socket tuples. Network result/raw request/response
fit one new fixed
private record <=16KiB (full bytes hex encoded), not the old six-file exporter.
After networking succeeds or fails, it closes network/record descriptors, performs
a bounded real fcntl scan permitting only0/1/2, emits a bounded instance/PID-bound
PRE_SHELL_FD_CHECKED stage line to its real PTY, and execs `/bin/sh -i +m` with the
existing fixed TERM=dumb environment in the SAME PID/session/foreground group.
There is no startup PTY command/key injection. Normal shell prompt/output and
later input use existing PTY behavior. No new job-control/background support.

Blocking libc lookup is owned by the enclosing finite PTY/application lifetime;
a stuck lookup must fail and be reaped, not be called an interactive fallback.
No claim that getaddrinfo itself is interruptible. New startup code must preserve
initial app002 DS4 checked descriptor sweep/internal READY/exec-pipe EOF: those
facts prove initial startup-program exec, not a shell already executing. The
second explicit fd sweep is a separate observed pre-shell fact; the existing
initial exec pipe cannot certify the later exec. Require actual fallback prompt
and same-PID/program-stage evidence; never claim an after-exec FD inventory.
The second closure boundary is an OPEN SOURCE PROOF OBLIGATION: inspect whether
libinfo/DNSService/dispatch starts threads or leaves concurrent descriptor
creators. A sequential close/recheck cannot prove a stable inherited set in the
presence of such activity. Do not implement or admit this direct second-exec
path until the quiescence/descriptor-creation facts or a different explicit
checked spawn design are reviewed. No shell-ready fact before actual prompt and
verified foreground group. Initial DS4 does not discharge this later boundary.
All allocations/env/argv for initial post-fork work remain prepared before fork;
only current async-signal-safe child setup/check/exec operations are permitted.

## Owners and teardown

The new outer parent uses the existing reviewed 56-byte pinned-SDK C spawn ABI
and CLOEXEC_DEFAULT (checked flags0x400e), not Rust Command or guessed opaque
structs. Selected child0/3=/dev/null; child1/2 are private nonblocking capture
pipes. It owns mdnsd and frontend positive PIDs even on local spawn cleanup error.
One event loop pumps bridge, child pipes and WNOHANG waits with bounded work.
It does not inherit bridge/utun/status/log endpoints into either child. Frontend
alone owns its two applications; each app alone owns its PTY session child.

Successful stop order is:

1. Request frontend shutdown at its finite boundary; retain mdnsd/utun/bridge.
2. Observe BOTH instances' one READY/one COMPLETE=1 CLEANUP, actual PTY raw wait,
   app reap/status EOF, synced six-file results/transcripts and new client record;
   verify manager audit/session seal, frontend wait0/stdout+stderr EOF and actual
   signal restoration. Distinct app/PTY PIDs and IDs must match observed records.
3. TERM only the still-owned mdnsd PID; wait normal EINTR4 rawstatus1024, no KILL,
   both stdio EOF, then retire only retained matching UDS/PID-file identities.
   Existing paths/replaced identities are refusals; no delete based on pathname
   existence or cached PID. Exact RAM resolv.conf nameserver10.0.2.3, absent DDNS,
   inherited real libSystem9fc and canonical runtime libdyld428e remain bound.
4. Exchange both CLOSE, drain all serial chunks/ACK credit, then EOF; no half-close
   while credit remains and no service/desktop writes after channel retirement.
5. Close the sole owned utun; verify full IPv4 route baseline restoration and
   interface absence, including cloned/default routes, then seal/return.

Failure still retires observed owned processes/interfaces/endpoints, recording
incomplete evidence rather than waiting forever for a missing acknowledgement.
A lost wait/ECHILD forbids later signals to the cached PID. A PID that was created
is never discarded on a spawn/cleanup error. No success from a kill call alone.
No direct RTM_DELETE, broad route/path cleanup, host routes/firewall, installed
m1n1, libc stubs, configd substitute, HID injection, GPU or physical-NIC change.

## Backend and evidence prerequisites

Existing selected one-flow policies are insufficient and must refuse this profile.
Root/backend owner must implement a separately reviewed bounded DNS/multiflow
contract before any candidate: at most two allowed instance nonce GETs, independently
owned TCP tuples, per-query ID/UDP tuple and first validated DNS lease/answer set,
nonincreasing TTL/narrow RR-rotation rules, cached DNS membership without invented
wire queries, fixed example.com/port80/read-only payload, bounded retries, bytes,
active idle, and close window. No arbitrary listener access or broad Internet
permission is inferred from an unchanged bridge wire codec. A proposed new serial
budget262144 bytes (two 2048-byte clients plus framing/retries) requires measured
host tests and explicit isolated profile admission; old131072 caps stay frozen. Proposed backend lifetimes are absolute420 seconds
from process start, accept240, active180, quiet150 and close15, each explicitly
recorded and finite. These account for the observed roughly150-second delay from
backend start to first target READY; an old300-second absolute cap is insufficient
for this new session budget. They require the new backend/profile, never a
silent extension of an old policy.

Both clients need complete private records + actual PTY output and independently
matching DNS/HTTP/TCP packets. Ordinary API success, actual HTTP200/EOF, fallback
prompt, frontend/app/PTY cleanup, daemon/UDS cleanup, channel closure, route
restoration and Linux recovery are separate predicates. No detached-process or
physical-input claim from titles, prompts, PID signaling, or source-only models.
