# Real DNS/HTTP over one owned default route

New finite Darwin client after the qualified public002 selected-peer test.
`guest/net_default.c` keeps one fresh utun, local10.0.2.15/32 and peer10.0.2.2,
through DNS, HTTP and protocol CLOSE. It links the separately frozen owned-default
adapter. The real kernel default route, successful ACK and independent actual
route/IFP/IFA readback precede descriptor exposure and network traffic.

The exact profile is `example-default-http-ack-v1` in network_default_experiment.
Markers are DL28; the actual unchanged Linux backend remains `example-http-ack-v2`
and its fixed example.com DNS/HTTP policy. This is a new experiment, not a replay
or relabel of public002. Result phases is1; it has no DNS handoff/boundary checkpoint.
Final success additionally requires default_add_attempted1, route_error0 and
route_stage5(CLOSED), interfacecleanup1/stage7 and actual whole-stream clean close.

A foreground root process checks the exact audited shell/launchctl/init ancestry
before configuring networking. Its UDP DNS socket sends one real A question;
TCP sends the fixed186-byte nonce-tagged GET to the first validated public A.
Raw DNS/HTTP bytes, exact process/route/interface observations and write outcomes
are exported. The host sends only acknowledged SPACE/LF bytes after its fixed
launcher, at most192bytes in flight. No host HID, alternate endpoint, redirect,
physical NIC, Wi-Fi, GPU observer, firmware or m1n1 change is involved.

The same owner is retained across both socket protocols; cleanup closes TCP/UDP
then the utun. No userspace route DELETE or unknown-ADD retry exists. Successful
teardown requires actual default absence and exact normalized baseline IPv4 route
configuration, including scoped-route detection, plus interface/peer/local absence.
The context lives on the heap. Raw observation budget is512KiB; each record is at
most4096bytes and hex chunks at most64bytes. Each write and the whole75s client
have checked monotonic deadlines. A record failure is sticky and disqualifies PASS.
After a primary error, cleanup recording remains enabled until its own writer
fails or the deadline expires. A partial previously written TX line may prefix the
first failure-only observation; successful observations occur at quiescent barriers.
The raw capture and failed framing remain authoritative; errors never become PASS.

The actual extracted parser and final-gate host tests cover270cases/8075checks.
The final gate rejects oldphase2, route failure/uncertainty, missing cleanup,
incomplete byte queues, absent CLOSE and observation failure. Source-bound Darwin
SDK EXECUTE build is libSystem-only against retained9fc. New typed host tests and
a real paced Linux DNS/HTTP fixture are separate prerequisites, not M1 evidence.
Frozen owner has2360wire/lifetime+541inventorychecks,7extracted exchange fault
cases and11compiledmutants. Its archive and every root source are hash-bound.

Before any target result, require an exact packet, fresh nonce, saved raw
Ccodec/ACK/byte ledger, independent route-lifecycle decoder and actual backend
DNS/HTTP equality. One controller cycle must end in verified Linux. Every failed
candidate remains preserved. A pass establishes this finite default-route path;
ordinary libc resolver/native mdnsd, persistent networking, native Wi-Fi and GPU
acceleration remain separate steps of the full goal.
