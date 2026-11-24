# Independent DNS retry evidence, revision 001

This original Python decoder performs no I/O or network operations. It imports
no production backend implementation. Its tests use synthetic IPv4 UDP packets
and the source-bound backend policy to check record compatibility. These are
host tests, not Darwin runtime evidence or a verdict on a saved target trial.

API:

    dns_retry_evidence(queries, replies, backend_egress) -> {checks, details}
    parse_dns_reply(dns_payload_bytes, original_query_bytes) -> parsed_details

`queries` and `replies` are ordered lists of actual complete IPv4 UDP packet
`bytes`, one through three per list. The caller extracts them from authenticated,
closed stream records, preserving every DNS packet without filtering rejected
packets. It supplies the closed **backend** `egress` object, not a paced fixture
consumer's later-clock egress object. Caller also bounds the source JSON file and
owns nonce, raw hashes, stream integrity, process/recovery and HTTP qualification.
A final-policy replay uses replay-clock evidence; it cannot establish historical
backend clock anchors missing from an older capture.

Every named check below must exist and be true. Missing or malformed input never
passes; `details.not_evaluated` distinguishes checks not reached after an earlier
failure. Details are JSON-safe and retain actual RR order, TTL groups, differing
byte offsets, raw/normalized SHA256 and recorded clock bounds. Unknown fields do
not substitute for required fields. Parsed DNS is at most 512 bytes / 32 RRs;
IPv4 is at most 1500 bytes; decompression has bounded backward-only pointers.

Required check keys (14):

- wire_packet_bounds
- query_identity
- reply_dns_structure
- tuple_and_id
- retry_budgets
- non_ttl_identity
- ttl_nonincrease
- original_answers_preserved
- backend_query_events
- backend_reply_events
- backend_original_records
- reply_window
- original_lease
- source_policy_revision

The selected query is exactly one example.com A/IN RD0100 question, with its
observed nonzero ID and ephemeral guest port retained across at most three
queries. Actual packet IPv4/UDP endpoints, lengths, fragments and checksums are
checked. IPv4 UDP checksum zero is permitted by this profile. All sections and
RR bounds are parsed; EDNS, bad compression, trailing bytes, ambiguous/cyclic
CNAME chains, zero authorization TTL and nonglobal terminal A addresses fail.

Only RR-header TTL words are normalized. For a reply whose entire answer is
A/IN/4-byte example.com records and whose NS/AR counts are zero, permutation is
allowed only with the same header/question, exact per-key RR multiset including
duplicate counts, and exact per-key normalized RR-wire multiset. Each key's
sorted TTL vector must not increase from the previous accepted reply. This does
not permit name/compression reencoding, exchanged TTL credit between IPs, new
addresses, classes, types, counts, authority/additional records or changed RDATA.
Other shapes require exact whole normalized wire and parsed semantic identities,
including fixed RR order, with nonincreasing wire-order TTLs. SOA minimum and all
other RDATA remain immutable; only actual header TTL words can change.

Every backend query/reply event is matched to actual packet bytes and parsed
fields, including accepted/duplicate booleans, record TTL offsets, original TTLs,
normalized RR bytes, comparison mode/hash/groups and order-change flags. Initial
answer order and duplicates, initial records and original lease remain fixed.
Backend policy revision must be integer 2 and the exact narrow policy string.

The exact exported first-query deadline anchor is required. All replies must be
within its inclusive 10-second window; duplicate replies must be strictly before
the first accepted reply's unchanged original lease expiry. Event and summary
anchors and remaining-lease arithmetic must agree exactly. These are backend
clock records, not independent capture timestamps. Equal recorded query/reply
times cannot independently prove interleaving, so a query at an equal timestamp
may have either correctly typed after_validated_reply flag. Other chronology
must agree. Actual TCP/HTTP selection and its lease are checked separately by
the caller against the original permission set.

Replay from the repository root:

    cd integrations/puredarwin/m1-network-resolver-service/evidence
    python -m unittest -v test_dns_retry_evidence test_dns_retry_mutants

The mutation test compiles and executes 12 temporary in-memory changes. Every
mutation must fail an actual assertion; syntax errors and execution errors do
not count as killed mutants. Frozen resolver_evidence.py and its existing
source-validation-001.json are unchanged.
