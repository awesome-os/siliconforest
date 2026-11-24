# example-http-v1: fixed egress admission

The wire remains frozen DLNET001: nonce24, per-direction sequence, OPEN/IPv4/
CLOSE, CRC32C, IPv4<=1500 and no inner resync. The caller supplies the fixed
nonce and the explicit host-library SHA256. The backend is Linux only and
creates one loopback TCP relay listener and one owned finite QEMU process.
There is no guest daemon, installed m1n1 change, SSH, host network device,
route, firewall rule or public listener here.

Before a public connection, permit only guest10.0.2.15 -> slirp DNS10.0.2.3,
UDP53, guest source port>=1024. The single question is uncompressed canonical
`example.com A IN`, RD set, one question and no answer/authority/additional.
At most three identical retries are allowed; a changed transaction, port or
question ends the trial. No system `getaddrinfo` substitutes for wire DNS.

The reply must reverse that exact tuple, match transaction and question, have
QR/RD, normal opcode, no truncation/reserved-Z/error, bounded counts and an
exact<=512-byte message. DNS names have bounded steps/length, checked backward
pointers and loop rejection. Only an IN A answer for the question or its single
unambiguous bounded CNAME chain grants access. Unrelated answer/glue records
do not grant addresses. Reject private, host-alias, loopback, link-local,
reserved, unspecified or multicast addresses, zero TTL, ambiguous chains and
more than four candidate A records. The entire reply fails on an invalid A in
the selected owner set. At most two byte-identical duplicate replies are allowed.

Authorize exactly the **first answer in that validated owner set**, TCP80 only,
for min(relevant TTLs,60s). No alternate-address retry, general DNS-to-firewall
adoption or cached authorization across sessions exists. Only one guest TCP
source port and SYN sequence may be used. All other destinations, including
10.0.2.2 host aliases, are refused. The QEMU socket is private; ARP is limited
to owned guest and fixed gateway/DNS aliases with checked lengths and identities.
Only IPv4 with20-byte header, no options/fragments, exact total length and checked
IP/TCP/UDP checksums is accepted (IPv4 UDP checksum0 is valid). TCP options may
remain opaque with bounded correct header length; they never change endpoint
or application-payload authorization.

The only outbound TCP application bytes are this exact request, segmented or
retransmitted consistently within its source sequence window:

```
GET / HTTP/1.1\r\n
Host: example.com\r\n
User-Agent: darwin-lab-egress/1\r\n
Accept-Encoding: identity\r\n
X-Darwin-Lab-Nonce: <the exact 48 lowercase hex nonce>\r\n
Connection: close\r\n
\r\n
```

No request body, cookie, credentials, POST, redirect follow or other host/URL
is enabled. Raw bytes in `http-request-expected.bin` are planned bytes; only
`http-request-observed.bin`, coverage counters and the packet transcripts prove
what was observed. A failed DNS trial therefore has an empty observed request.

Return TCP must match the selected tuple and SYNACK sequence/ACK. Reassembly
is bounded64KiB, rejects inconsistent overlaps and preserves actual bytes. A
response must be HTTP1.0/1.1 status200, exact text/html type, identity encoding,
unambiguous Content-Length/chunked/close-delimited framing and contain the
observed `Example Domain` text. Header cap8192, no duplicate headers, chunk
extensions/trailers or extra body bytes. Incomplete input never becomes success.
Require the exact request coverage, complete response and corresponding server
FIN without a gap. The public fixture additionally observes ACK of client FIN.
HTTP/DNS are unencrypted: this establishes observed network behavior through
the selected public IP, not cryptographic IANA/operator identity or fresh upstream
DNS traffic beyond the actual slirp resolver reply (which can be cached).

The backend inherits the original bounded lifecycle:8 frames/queue,4096 frames
and4MiB payload/direction,5MiB each raw transcript,16KiB diagnostics pipe,
4096-byte reads,<=50ms polls,2s continuous queue/partial frame/close EOF,
10s active idle,5s ARP readiness,<=300s accept and<=120s active lifetime.
QEMU32MiB has no vCPU/device workload, disk/display/serial/monitor; `user`,
`socket` and two `hubport` backends provide slirp. It exits with its parent.
Normal close requires validated peer CLOSE/EOF, local CLOSE drained, policy
completion, empty output queues and clean child wait/reap. Any error is sticky.
No ignored Policy exception may be retried; only earlier valid observations
remain committed. Host parser/unit tests are synthetic evidence only.

All resources stay owned: checked NONBLOCK/CLOEXEC, positive PID adopted before
local endpoint cleanup, TERM only unreaped QEMU, wait2s/KILL/wait2s, actual status
recorded. KILL/nonzero child status/cleanup error fails the result and exit code.
Raw `relay-{rx,tx}.bin` carry received/encoded DLNET bytes and
`ethernet-{rx,tx}.bin` carry QEMU32-bit length+Ethernet. TX is **encoded** until
the separate actual-written counters account for it. Final closed records bind
length/SHA256; no content filtering turns a failed DNS/HTTP trial into a pass.
