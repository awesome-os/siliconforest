# Native resolver example.com backend revision003

DNS policy stays revision2. This separate opt-in Linux backend uses profile `native-resolver-example-http-v1`.
It observes network packets; native daemon execution, libc getaddrinfo results,
owned-default-route evidence and Linux recovery must be established separately.
No synthetic guest result qualifies any of those facts.

This leaf derives from frozen native-resolver backend002 at `m1-network-resolver-egress/revisions/002/host`, whose hashes are recorded in
`source-validation.json`. The separate old public-egress revision002, resolver001
files, all failures and all profiles remain unchanged. All new changes are original lab code; no GPL code copied.

The exact pinned POSIX mDNS unicast A path sends a 29-byte question: nonzero
observed transaction ID, RD=0x0100, qd=1, an/ns/ar=0 and lowercase example.com
A IN. Its ordinary retries retain the question ID and shared ephemeral port.
The profile accepts at most three byte-identical queries on that same port;
no nonce-derived DNS ID is required. EDNS/OPT, AAAA, another question, endpoint,
ID/port change or unexpected local/multicast traffic fails closed without
forwarding that packet. The source-facts file records why this narrow scope is
appropriate. It does not assert that a real daemon cannot encounter other paths.

A reply must arrive from 10.0.2.3:53 to the observed query port within ten seconds,
match its transaction/question and validate a bounded CNAME/global-A chain.
The first validated response supplies a lease capped at 60 seconds. At most two
reply duplicates may have nonincreasing TTLs; they never refresh or replace that
original lease, first canonical reply, A order, address set or selected endpoint.
The first accepted elapsed time and original expiry are fixed and emitted in
every reply event. The exact first-query source clock anchor is also recorded
as dns_first_query_elapsed_seconds. Remaining lease is computed from those same emitted anchors.
Raw wire bytes, RR order, per-record TTLs and comparison decisions remain saved.

Only when NS=AR=0 and every answer is A/IN/4B owned by exact example.com may
records permute. The header and question stay exact; the exact multiset of
(owner,type,class,IPv4), per-key duplicate counts and per-key normalized RR wire
bytes stay equal. Owner encoding or compression changes are not permitted. Each
key's sorted TTL vector must be nonincreasing relative to the previous accepted
reply. Thus another IPv4, owner, type, class, count or TTL increase is refused.
All other response forms retain full normalized DNS byte identity with only RR
TTL words zeroed, plus parsed owner/CNAME semantic identity; their RR order stays
exact. Every RR TTL in answer/authority/additional sections must be nonincreasing.
The original lease and ten-second DNS window remain required. An unsolicited OPT response is unsupported. A non-global chain
address, zero TTL, malformed response or unauthorized changed duplicate fails closed.

The first SYN may choose any global A in that first response, because libc can
reorder the wire answers. Its flag byte must be exactly0x02 (ordinary SYN) or
0xc2 (SYN+ECE+CWR classic ECN). No other first flag byte, reserved/NS bits or
SYN payload is admitted. Pinned XNU tcp_output explicitly emits that ECN form.
This is the only enforcement-condition change from backend002; DNS policy2
and all tuple/answer/flow/checksum/nonce GET checks are unchanged. The initial
flag byte is recorded in initial_tcp_flags and backend_policy_revision is3. The chosen address, wire-answer index and source port
then lock one TCP connection to port80. No fallback to a second flow/address is
permitted. HTTP remains exactly the inherited 186-byte GET with Host example.com,
User-Agent darwin-lab-egress/1, identity encoding, fresh 48-hex nonce, and close.
Response bytes, full request coverage, contiguous response and actual server FIN
are required; TLS/DNSSEC/server identity is not claimed.

Runtime limits: active at most90s, idle30s, post-CLOSE EOF15s, accept at most300s.
DLNET001 codec is pinned to SHA256 698a007fb9dbc63fd5eabac46163f7a7ce6a7ff3056a37ed62185f8e9a6d2885.
The backend uses a child QEMU machine-none libslirp network and loopback relay,
not a CPU guest, TAP, firewall rule, host route, target or hardware adapter.

The ready/result APIs retain the inherited shape and use the new exact profile.
`egress.dns_query_events` and `dns_reply_events` include raw bytes, IDs, ports,
relative monotonic times, retry/duplicate state, original fixed expiry, remaining
lease, comparison mode/hash, wire and per-key TTL vectors, and actual RR order
changes. `dns_first_reply_rrs` stays canonical. `dns_policy_revision` is 2. `dns_answer_records` preserves
answer owner/type/class/TTL/raw RDATA; selected_answer_index is null before the
first SYN. DNS and HTTP binary records plus full relay/Ethernet transcripts are
saved and hashed, including on failure. An unsupported packet remains in the raw
transcript even when it never reaches the accepted-event arrays.

Offline checks:

```
python3 -I -m unittest discover -s integrations/puredarwin/m1-network-resolver-egress/revisions/003/host -p 'test_p*.py' -q
python3 -I integrations/puredarwin/m1-network-resolver-egress/revisions/003/host/test_mutations.py
python3 -I integrations/puredarwin/m1-network-resolver-egress/revisions/003/host/verify.py
```

`test_backend.py` runs real local QEMU lifecycle/refusal cases without public
packets; `test_idle_bound.py` measures the real30s idle bound. The explicit-opt-in
`test_public_pipeline.py` sends source-fact synthetic query bytes through the
immutable production DL29 helper and actual Linux libslirp/DNS/HTTP. It selects
the last validated answer to exercise libc answer reordering. Both actual helper and backend profiles are native-resolver-example-http-v1;
the helper has the same immutable DLNET001 transport and emits DL29 markers. It does not run mdnsd,
Darwin, getaddrinfo, create an interface/default route or emit a fake RESULT.
The ECN paced fixture sends a real0xc2 initial SYN through QEMU/libslirp and uses
the full existing three-query/two-duplicate budget
with measured ~1-second retry gaps. It saves actual first-reply credit state and
raw TTL/order outcomes, then completes HTTP/CLOSE/EOF without requiring a random
upstream ordering choice. Actual trial005 observed TTL166→165→164 and rotation.
Actual trial003 rotation bytes also replay through the final policy and frozen
independent checker as a source regression only. Earlier strict draft failures
and trial004's fixture-only missing-rotation coverage condition are preserved.
No M1 cycle is rerun/regraded by these fixtures. No fake DNS server or resolver
is used. The independent checker validates DNS evidence only; native resolver
qualification remains root's separate saved-target test.

`replay_ecn_syn.py` validates CRC, IP/TCP checksums and the real44-byte TCP
header from the failed resolver002 capture, replays its observed DNS bytes,
then compares old rejection with new admission using an explicitly synthetic
replay clock. It never rewrites that target verdict, invents CLOSE/HTTP, or
claims a new Darwin run. TCP source facts are in tcp-ecn-source-facts.json.
