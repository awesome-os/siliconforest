The original revision005 checker extends frozen independentTCP6 source948653fb
with TCP7 emission-backed FIN/ACK credits. It imports only Python standard modules,
never the production backend/parser/model. Older checkers remain unchanged.

APIs:
- terminal_reset_evidence(guest_TCP_packets, host_TCP_packets, egress,
  *, host_frames=actual_full_host_frame_byte_list)
- terminal_reset_stream_evidence(guest_stream_bytes, host_stream_bytes,
  egress, nonce)
Both return the fixed14 checks/details and qualified_TCP7_terminal_reset_records_only.
Missing, malformed or unclosed input fails. Raw wire directions are1guest→host and
2host→guest. Host-frame identities are derived from nonce/sequence/CRC32C/header/
payload bytes, never accepted as opaque producer claims. The stream entrypoint
checks both complete OPEN/IP/CLOSE streams and unchanged global128frames and
65536IPv4bytes per direction before calling the same TCP/credit checker.

Every actual exact normal20-byte pure FIN/ACK has one emission record bound to
its unique actual direction2 frame sequence/SHA and raw packet SHA. First perflow
emission is baseline and grants no credit. Subsequent emissions grant sequential
credits. Guest terminalRST04 must consume one FIFO unused FINACK credit and name
its exact frame identity; peer statelessRST/ACK14 must consume one FIFO actual
guestRST credit. Recorded emission/consumption counts, flags, source clocks,
indices and balances must agree. Unused credits are allowed. Bookkeeping limits
8bare/127guest/127stateless/128totalpeer grant no permission without raw causal
credits and global bounds. No additional tuples, payload or control forms.

Actual frame presence proves the recorded bytes/identity; queue acceptance and
cross-direction emission-before-consumption remain source-bound commit-hook facts.
Split directional streams do not independently establish a shared chronology.
Queue commitment does not prove physical serial delivery. Caller separately binds
closed raw files, forwarded/delivered equality, physical ACK accounting, process
ownership/cleanup and Linux recovery. No Darwin verdict comes from Linux fixtures.

Tests include a five-credit golden, exhausted and baseline negatives, coherent
FIFO swaps/reuses with otherwise plausible balances, absent/uncommitted frames,
exact SHA/sequence/CRC/session/direction, strict schemas, normal20 eligibility,
source chronology, and control/HTTP/global bounds. Actual008 complete direction2
FINACK frames are parser-only positives; its failure and missing CLOSE are never
upgraded. Final actual-fixture-validation-002.json binds the current checker to
the new closed Linux fixture. 001 is an earlier draft validation before tightening
integer-only bookkeeping declarations. The backend source receipt discloses its
pre-run reconstructed source versus final diagnostic-only label delta; no exact
final-backend-bytes execution claim is made here.
