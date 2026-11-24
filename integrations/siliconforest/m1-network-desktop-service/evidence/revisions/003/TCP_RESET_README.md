# Independent TCP5 terminal-control evidence

This distinct revision preserves the original TCP4 checker and its receipts.
`terminal_reset_evidence(guest_packets, host_packets, egress)` accepts two ordered
lists of actual IPv4 TCP packets plus the closed source-bound backend record.
It returns fourteen fixed checks, bounded diagnostics, and
`qualified_TCP5_terminal_reset_records_only`. Missing or invalid inputs fail
closed. There are no network calls or producer parser/model imports.

The original independent IPv4/TCP checksum, tuple, SYN/ISN, exact186-byte GET,
HTTP200/reassembly/FIN and reset-byte correspondence checks remain. A reset
records one of two exact triggers. `pure_final_ACK` needs the existing raw FIN,
server FINACK and pure final guestACK. `repeated_client_FIN` instead needs at
least two raw exact clientFIN packets (same finalrequest sequence/serverFIN ACK),
the raw server FINACK, and the explicit source-bound
`repeated_client_FIN_after_server_FIN_ACK` grant. Both need prior client/serverFIN
facts, exact no-payload flags04/ACK0/serverFIN+1 reset, per-direction packet order,
count<=4 perflow and backend3/TCP5/DNS2 metadata. No trigger grants endpoint,
payload or DNS authority.

Separate directional streams do not establish global packet chronology.
`details.global_chronology_independently_observed` remains false. The predicate
that the repeated FIN was accepted after source observation of the serverFINACK
comes from the frozen backend guard and its specific grant flag, not a fabricated
cross-direction clock. The caller separately verifies CRC/frame nonces/sequence
and equality of actual host input packets with forwarded relay packets.

Only the new actual Linux FIN-retry fixture supplies actual-runtime source test
evidence. Tests also cover the old pureACK path and reject oldTCP4 versions or
missing newflags; they do not regrade or upgrade the failed M1 attempt005. Its
separate raw-byte source replay is explicitly synthetic compatible scheduling.
No native API/PID, desktop, DNS lease, route or recovery claim comes from here.

Run `python3 -B -I test_tcp_reset_evidence.py` and
`python3 -B -I test_tcp_reset_mutations.py`. Mutation tests compile isolated
Python variants without editing this module or the frozen backend.
