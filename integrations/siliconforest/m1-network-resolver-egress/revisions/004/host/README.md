# Resolver drain backend004

New explicit profile `native-resolver-example-http-drain-v2`, backend revision4,
DNS2/TCP3. policy.py is BYTE-IDENTICAL to frozen00320a0b6. No DNS, TCP, endpoint,
nonce GET, ECN, checksum or flow authority changes. Only the wrapper profile and
active/post-CLOSE budgets change, with diagnostic phase snapshots added.

Active time is at most120 seconds; post-CLOSE drain40; idle30 before closing;
accept at most300. No progress refreshes either whole limit. The EOF timer still
starts when the host CLOSE is queued on the raw relay: it is explicitly NOT a
physical-target delivery timer. Forty seconds is a bounded allowance for the
known serial-credit backlog. It never substitutes for BOTH codec CLOSEs, all
physical target credit, relay half-close/EOF, complete HTTP/FIN or process cleanup.
Source metadata names backend4/tcp3/dns2 distinguish the unchanged packet policy.

The corresponding isolated helper/guest profile uses whole105 and close40,
unchanged192-byte credit/fixed3-second ACK deadline, frame/queue/131072-byte caps
and DLNET001 wire codec. Unknown traffic, arbitrary endpoints, packet drops,
synthetic server responses, routes/firewall/TAP, hardware or m1n1 are not added.

phase_events has at most32 diagnostic snapshots: validated guest CLOSE, host
CLOSE queued/timer origin, raw socket drained, peer EOF received/validated and
failure. It records actual relative monotonic times and queue/write/frame state.
Raw socket drain is not target acknowledgement. The helper has its separately
bounded close_timeline; clocks from different machines must not be conflated.

Actual causal fixture: `test_close_causal.py --case old15|new40` (explicit public
network opt-in). It uses real QEMU/libslirp/DNS/HTTP, SYNc2 advertising MSS1460,
and paced target-credit simulation. After real application HTTP/FIN it sends
client FIN and guest CLOSE immediately, with no FINACK wait. It forwards every
actual queued response retransmission. A deterministic closing pace from the
observed queue reserves6000 bytes of tail and about22 seconds of drainage; each
chunk is192 or less and ACKed well inside3 seconds. No response packet is
manufactured, rewritten, suppressed or treated as successfully delivered early.

The unchanged003/15 control must fail closed with valid HTTP and undrained
credit. New004/40 must consume/ACK all bytes, both CLOSEs and EOF, with an actual
closing duration greater than15 and less than40. These are Linux fixture facts,
not a Darwin RESULT, hardware boot or recovery verdict. `test_public_pipeline.py`
is a convenience entry to the new40 case. Existing001–003 sources/captures and
failures are preserved. Root separately owns the new guest and M1 admission.
