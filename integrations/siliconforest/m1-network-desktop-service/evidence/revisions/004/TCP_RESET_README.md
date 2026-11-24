# Independent TCP6 terminal-close evidence

This NEW revision preserves every older checker, backend and failed capture.
`terminal_reset_evidence(guest_packets, host_packets, egress)` consumes TCP-only
ordered IPv4 packet bytes per direction plus the source-bound closed record.
It returns fourteen fixed checks, bounded diagnostics, and
`qualified_TCP6_terminal_reset_records_only`. Missing or malformed inputs fail
closed. No network or producer parser/model is imported.

The existing independent checksum, exact2tuple/SYN/186GET/HTTP200/contiguousFIN
and raw bare-reset correspondence checks remain. Bare peer04 permits only its
existing exact closed-flow pureACK or repeatedFIN trigger, now at most8. This
is declared finite profile headroom, not a normative TCP count.

A guest-terminal04 must be exact20-byte TCPheader/reserved0/no payload,
SEQclientFIN+1/ACK0 on the completed locked flow. Its numbered raw source event
creates one of at most4 single-use credits. Only an exact reverse peer14 with
20-byteheader/SEQ0/ACKthatguestSEQ/no payload may consume one, at most4 replies.
All packet/event bytes, scalar fields, count declarations and credit identifiers
are checked independently. Total peer resets must be<=12. Unused guest credits
are valid: a peer can ignore a guest reset. Reuse, unknown credit, overconsumption
or a falsely marked consumed credit fails. No payload or endpoint is authorized.

Separate directional streams do not establish a global packet clock. Raw control
order is checked within each direction. The cross-direction FIN guard and credit
order remain source-bound facts; source event clocks must order each guest credit
before its consuming peer reply. `global_chronology_independently_observed` remains
false. The caller separately proves CRC/frame session identity and equality of
actual host input packets with forwarded relay packets. The installed libslirp
branch is not verified here; only observed packet correlation is checked.

Actual-runtime source tests use ONLY the NEW Linux terminal-close fixture,
independently CRC32C/OPEN/CLOSE decoded. Tests preserve old pureACK/FIN behavior
and reject oldTCP4/TCP5 versions or missing new fields; they never upgrade005/006.
No nativeAPI/PID, DNS lease, desktop, route or recovery claim follows.

Run `python3 -B -I test_tcp_reset_evidence.py` and
`python3 -B -I test_tcp_reset_mutations.py`. Compiled Python mutants are isolated;
no production, frozen backend or capture is edited by the mutation suite.
