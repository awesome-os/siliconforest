# Finite desktop network host profile

This separate DL30 overlay admits only `desktop-native-resolver-two-http-v1`
under `network_desktop_experiment`, the native50 KC, passive native input and
sealed launcher. It keeps installed m1n1 and the recovery controller unchanged.

The parent owns one mdnsd and the unchanged shared frontend007, which owns two
new network-terminal instances. Clients perform their own libc lookup and HTTP;
the host neither fabricates results nor chooses an address on their behalf.
Whole guest180 seconds, frontend90 plus15 cleanup, daemon5, close40. The root
controller, supervisor and worker each require runtime285 and worker345 only
for this exact profile. Legacy authorization and its300-second worker guard
remain unchanged.

Frame128, queue8 and physical192-byte/3-second ACK limits remain enforced. The
new explicit SPACE/LF cap is262144, bounded coalesced reads reserve a queue slot
before receiving another frame, and all pending credit, both CLOSEs, partial
bits/lines, relay EOF and executable exit0 remain required. No packet drops,
queue expansion or ACK deadline refresh. Source observations are closed records
and hashes, never substitutes for independent wire/PID/lifecycle checks.

Tests exercise the real isolated overlay, both pre-reboot and worker admission,
legacy suites, coalescing/backpressure and assertion-detected decision mutants.
They do not boot Darwin or establish desktop networking. The backend's actual
paced two-flow fixture and packet bindings remain separate required gates.
