# Independent terminal-reset byte evidence

`terminal_reset_evidence(guest_packets, host_packets, egress)` consumes two lists
of actual IPv4 TCP packet bytes, each preserving that direction's observed order,
and the source-bound closed backend `egress` record. It returns fourteen fixed
`checks`, bounded `details`, and `qualified_TCP4_terminal_reset_records_only`.
Missing or malformed inputs produce false checks and diagnostics, not exceptions.
It performs no filesystem, socket, target, C-classifier or producer-model calls.

The original parser independently checks IPv4 and TCP checksums, fixed addresses,
full tuples, SYN/ISNs, exact 186-byte same-session requests, response reassembly,
HTTP200 framing and FIN. Every reset must match the recorded raw packet and the
finite TCP4 terminal shape/count. Flows with resets require actual raw clientFIN,
serverFINACK and final pure guestACK packets plus valid per-direction control
order, as well as all three explicit source-bound backend grant flags. Flows
without resets do not need the exceptional reset grant.

Separate directional packet streams do not prove global packet chronology.
`details.global_chronology_independently_observed` is always false. The backend
TCP4 source guard, not a reconstructed target timeline, supplies that remaining
cross-direction authority. This module does not claim client PID/API attribution,
DNS leases, forwarding, CRC, desktop, routes, physical input or target recovery.
The caller separately proves equality of actual host input packets and forwarded
relay packets, and their original CRC/sequence/nonces. Tests independently check
CRC32C and OPEN/CLOSE on the retained actual Linux004 relay streams before replay.
No target capture is regraded by this source-validation task.

Run `python3 -B -I test_tcp_reset_evidence.py` and
`python3 -B -I test_tcp_reset_mutations.py` from this directory (or absolute paths).
The mutation suite compiles isolated Python source variants and executes the
same tests; it never edits the production module or frozen backend.
