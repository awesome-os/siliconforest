# Two-client desktop network backend contract 001

Source/Linux-validation profile `desktop-native-resolver-two-http-v1`. This includes an original bounded packet model and actual QEMU/slirp wrapper.
It does not admit a target candidate or prove a networking daemon or desktop. Frozen resolver001–004 and all
failed/passing captures remain unchanged.

Root approved separate clocks: Linux backend absolute420 seconds from creation,
accept240, active180 from OPEN/READY, quiet150, CLOSE/EOF40, with the earliest
whole deadline winning. The M1 worker285-second bound (<=300) has a different
origin. Quiet time measures accepted network packets, not arbitrary console
noise. The old idle30 rule is not used by this profile. An issued <=192-byte
SPACE/LF chunk still requires its unchanged fixed3-second ACK deadline; partial
ACKs never refresh it. Proposed new serial budget262144 is explicit to this
new profile and is tested by a Linux paced fixture before any target admission.
Packet cap128/direction, IP bytes65536/direction, queue8, MTU1500, two flows,
HTTP response2048/flow and exact186-byte requests bound the packet model.

Only 10.0.2.15 may send the canonical29-byte example.com A/IN/RD question to
10.0.2.3:53. The first actual mdnsd source port is fixed for its one owned
lifetime. At most FOUR actual query packets total, including retries, may
appear. Each new nonzero ID needs an actual query; byte-identical same-ID
retries do not create a new lease. Completed/expired ID reuse is unsupported,
not interpreted as a renewal. No query or response is invented for a cache hit.
Each reply is correlated to the exact recorded ID/question/UDP tuple and its
own10-second request window. Up to two duplicates per ID are bounded; their
comparison rules are frozen DNSrevision2: narrow pure-A record permutation,
exact per-key multiplicity/wire identity, nonincreasing per-key sorted TTLs;
other forms keep full normalized wire/semantic identity.

Each ID's first validated response keeps its original answer list and expiry.
Its lease is min(raw minimum CNAME/A chain TTL, remaining active/absolute whole
budget), with NO old60-second cap. This lets real mdnsd cached answers remain
usable during the90-second desktop when their original TTL permits it. No
duplicate refreshes that expiry. A real new query/new ID and validated reply
may create an independent renewal lease, within the same four-query budget.
No fake getaddrinfo/host-resolver call/configuration or hidden budget expansion.

A new TCP SYN must have flags exactly02 orc2, no payload/reserved/NS bits, source
port>=1024, destination80 and a global A in a currently unexpired observed lease.
Select one matching lease and record its ID/index/expiry at SYN. At most TWO
actual full TCP four-tuples may be admitted, in either client order. Both must
send exactly the inherited186-byte GET with the SAME fresh fullsession nonce,
Host example.com, User-Agent darwin-lab-egress/1, identity and close. No fallback
address/third flow/other method, request suffix or arbitrary endpoint. An
admitted same flow may finish after its DNS lease expires, under the unchanged
whole deadline; expiration never authorizes another SYN. Responses require
actual contiguous bytes, HTTP200/Example Domain, complete GET coverage and FIN.

Flow ordinal is observation order, not a client slot or PID. Two source ports
may coincide only with distinct full tuples; a tuple/ISN is never reassigned.
The clients—not the host—record their actual getsockname/getpeername tuple,
PID, instance, slot, ordered getaddrinfo list and first selected API address.
Final attribution is posthoc against the exact wire tuples, not a way to grant
backend authority. Native-terminal's new startup variant will derive the full
session nonce from the exact trusted0700 records root and validate its instance
prefix; existing instance48 lacks four nonce hex digits. No frontend CLI or
shell-command expansion is proposed. The producer's final record grammar and
lifecycle require separate source binding before this model can qualify it.

Unknown traffic, private origins/answers, another DNS server/name/class/type,
port changes, mode writes, HID injection, GPU/Wi-Fi/TAP/routes/firewall and a
macOS kext are outside the profile. Pure model success is packet-policy evidence
only. Real service readiness, client/PID/API attribution, desktop rendering,
PTY/frontend/daemon cleanup, utun/default baseline restoration, transport EOF
and verified Linux recovery remain separate required target predicates.


TCP revision4 adds one finite cleanup-control case established by failed Linux
fixtures001/002. On an already locked tuple, flags04/ACK0/no payload/reserved0
and sequence exactly serverFIN+1 is accepted only after complete exactGET,
validated contiguous HTTP200+serverFIN, observed guestFIN at finalrequestseq,
observed server pureACK acknowledging thatFIN, and a later purefinalguestACK.
At most FOUR such identical terminal resets perflow may pass. Raw resets are
recorded and forwarded through the codec/physical channel; they create no new
flow, DNS authority or payload authority. Early, changed, ACK-bearing or other
resets remain fatal. Frozen TCP3 sources are unchanged; this is backend2/TCP4.

The actual paced fixture binds session/host/line_bridge.py by exact SHA256,
never the archival fixture-only credit bridge. Both real Linux HTTP flows must
complete physically, then the transport stays open at least90 additional seconds
before exact bothCLOSE/full192byte-credit/EOF cleanup. Late accepted terminal
resets are delivered and recorded; quiet150 is still a finite activity bound,
not a requirement for90 seconds of silence. Backend admission observation and
physical simulated-target delivery have distinct clocks/orders under queued
SPACE/LF pacing. The fixture does not fabricate a Darwin RESULT or recreate
host-owned FIN-order grants from that different physical observation order.
Actual raw logged packets, backend source-bound state and all queued physical
bytes remain available for independent correspondence checking.
