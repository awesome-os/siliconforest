# Isolated resolver-drain host revision002

New exact typed network_resolver_experiment version2, profile
native-resolver-example-http-drain-v2: seconds105, close_seconds40,
backend_seconds120. All other nonce/endpoint/GET/native-input fields retain their
exact prior values and types. Only nonce48 and owned relay_port vary. Oldnetwork
profiles, oldversion1 and unknown fields are refused before serial opening.
The same gate is used by preflight and worker; native50 f882 is still required.

The isolated net-resolver-drain-001/lab derives from frozen net-resolver-001/lab.
Only its launcher/controller change and one new helper is added. Frozen old
sources, inspector, bootstraps, input helpers, controller/recovery implementation,
kernel, installed m1n1 and captured failures remain unchanged. No deployment or
hardware actor is part of this source validation.

DL29 wire framing/CRC/direction/nonce/sequence,192-byte credit/fixed3-second ACK,
frame128/queue8/byte131072 caps and both-CLOSE/all-credit/EOF predicates stay
unchanged. New helper whole bound105. No packet/duplicate is dropped. Wrapper
BEGIN must name the exact new profile/backend; wrapper timing never grants I/O.

RESULT retains its prior fields and appends exactly four uint64 values:
close_started_ms close_deadline_ms close_completed_ms finished_ms. A PASS needs
0<started<=completed<deadline, deadline=started+40000, finished>=completed,
plus all prior child/interface/route/HTTP/frame/credit/CLOSE checks. Coarse clocks
may record completion at the same millisecond as start. This is a consistency
check of guest reports, not an independent target clock or authority source.
The source-bound guest guard and actual packet/recovery records remain required.

close_timeline is diagnostic-only bounded last-event state: guest CLOSE observed,
host CLOSE received, last frame fully issued, last cumulative ACK, SHUT_WR,
relay EOF, RESULT and failure snapshot. Failure snapshots preserve queue/offset
before cancellation clears them. Half-close still waits for all actual target
credit; neither timestamps nor RESULT fields bypass that predicate. Clocks from
separate machines cannot be compared without a separate clock relationship.

31 helper,18 profile/writer,41 legacy-host and20 controller fake-only tests pass.
The public fixture wrapper delegates to the hash-bound actual backend004 causal
fixture with no fake Darwin RESULT. Its old15 negative and new40 positive use
real queued TCP retransmissions and measured credit timing. The fixture proves
a Linux transport path only. Root owns the new guest SDK and target admission.
