# TCP7 committed FINACK credit profile

This distinct source leaf is backend5/TCP7/DNS2 under the unchanged explicit
`desktop-native-resolver-two-http-v1` wire profile. Older backends, captures and
failures stay immutable. It grants no new tuple, ACK, sequence, payload, option,
DNS, method, flow or mode authority.

Only two locked flows may send the same canonical186-byte session-nonce GET to
validated unexpired global example.com A answers. Real observed mDNS queries,
raw-TTL leases, bounded retransmissions and independent per-ID original expiry
are unchanged. Complete HTTP200/contiguousresponseFIN and exact originalclientFIN
plus source-observedserverFINACK remain required for terminal controls.

A pureFINACK is exactly TCP20/reserved0/no payload/flags10 with serverFIN+1 SEQ
and clientFIN+1 ACK. It is eligible only on that completed locked flow. The new
commit hook runs AFTER codec encoding, transcript recording and successful
append to the bounded forwarding queue. It verifies the last validated packet,
full DLNET001 kind2/direction2/nonce/CRC/payload, and unique increasing hostframe
sequence within127. A failed encode, record or queue append creates no credit.
No claim of physical delivery follows from committing to the source queue.

Every eligible committed FINACK is recorded with raw packet/frame identities
and the existing same-per-flow elapsed clock. The first is a baseline and grants
nothing. Each subsequent emission grants one numbered single-use duplicateACK
credit. The exact guest-terminal04 form (20-byteheader/reserved0/no payload,
SEQclientFIN+1/ACK0) consumes the oldest unused FINACK credit. An exact reverse
peerstateless14/SEQ0/ACKthatguestSEQ/no payload consumes the oldest unused actual
guestRST credit. Unused credits may remain. Barepeer04 keeps the earlier exact
pureACK/repeatedFIN trigger and its cap8. Everything is recorded and forwarded;
there is no drop, fabricated ACK, response, address or payload.

`terminal_budgets={bare_peer:8,guest:127,stateless_peer:127,total_peer:128}` gives
only finite storage ceilings derived from existing frame128. It is not authority:
actual distinct emitted FINACK credits and actual guestRST credits, plus the
GLOBAL128frame/65536raw-per-direction/262144SPACE-LF/192byte fixed3sACK/queue8,
are always stricter. Whole clocks remain absolute420/accept240/active180/quiet150/
close40. No flow receives a per-flow promise that global capacity is unused.

Metadata uses finACK_emission_events with source-frame sequence/SHA/rawpacketSHA,
commit_index=frame_sequence-1, per-flow emission_index, baseline, credit_id and
consumption. Guest events reference consumed FINACK credit/frame identity. Peer14
events reference the actual guestRST credit. All those elapsed clocks have the
same per-flow origin. These are source-bound ordering facts; separate directional
raw streams never prove an independent shared historical or physical clock.

The actual Linux fixture must record six committed FINACKs, exclude the baseline,
produce five actual guestRST controls and real credited peer14 replies, ignore
incomingRST, hold90s after both physicalHTTPFIN and complete all physicalcredit/
bothCLOSE/EOF/QEMUreap. It remains a synthetic guest/TCP client over real Linux
slirp/DNS/HTTP, not a Darwin API/client/PID/desktop/route/recovery proof.

The fixture executed pre-label source. Two unexecuted diagnostic error strings
were subsequently corrected; a reconstructed pre-edit source, exact diff and
AST comparison are recorded separately. No contemporaneous pre-edit snapshot or
fixture execution of final source bytes is claimed. Final unit/mutation tests
exercise final source. No installed libslirp branch verification is claimed.
