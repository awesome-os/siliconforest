# Later Darwin extension: no integration in this leaf

Root's first link001 remains fixed host-alias TCP/UDP and its source/packet
criteria are unchanged. This egress backend is a separately selected future
profile. It cannot be substituted silently for transport001. Root owns any
guest daemon, SDK build, console/SSH/controller change, target trial and recovery.

The future guest client should open its own UDP socket, generate and record a
fresh DNS transaction, and send the exact fixed A question to10.0.2.3:53 while
the same nonblocking utun pump runs. Validate source tuple/ID/question/rcode/TC
and bounded CNAME/A records from the received bytes, then choose the same first
validated A address as the backend. Create one nonblocking TCP socket to that
IP:80, check writable completion with `SO_ERROR`, write the exact nonce-bearing
request, resume partial writes and read the bounded response until orderly EOF.
Validate the actual status/framing/content, then close the owned socket and
complete DLNET CLOSE with finite grace. A successful socket connect/write alone
does not establish HTTP or DNS. Do not call system DNS configured until its
resolver/daemon/configuration path is separately demonstrated; this direct UDP
client avoids inventing a working mDNSResponder/system resolver.

The existing utun is10.0.2.15/32 peer10.0.2.2. DNS3 and the selected public IP
need guest routes; host routes/firewall remain untouched. For the first fixed
egress proof, prefer exactly two owned guest /32 routes (DNS3, then selected A)
through the utun peer rather than altering a default route. Snapshot/refuse
conflicts first. The local pinned XNU `bsd/net/route.h` declares AF_ROUTE
`rt_msghdr` (PID, sequence, errno), RTM_VERSION5, RTM_ADD1, RTM_DELETE2 and
destination/gateway/netmask address bits. Implement with actual pinned SDK C
types and `sockaddr_in.sin_len`; do not guess LP64 struct layouts or sockaddr
padding from Python. Compile/static-check the exact target ABI before a boot.

Route creation success must come from a bounded matching kernel routing-socket
reply (version/type/PID/sequence/errno), not successful write alone. Confirm the
actual destination/gateway/interface after adding. Record exactly what this
process created and delete only the still-matching owned entry during cleanup;
never remove a pre-existing route. Closing utun removes the owned interface;
explicit route cleanup outcomes still need evidence. Default0/0 is a later
separately reviewed option after exact-route egress: refuse existing default
conflicts, record/add/verify/delete the owned route and demonstrate actual
resolver and TCP/UDP behavior. No host-side default-route change is required.

The root-owned opt-in console ingress alphabet remains SPACE/LF only, eight
MSB-first bit-lines per DLNET byte. Every arbitrary suffix is whitespace; no
command/HID is sent after the fixed launcher, including daemon death mid-input.
Egress is separately bounded nonce-labelled hex. Preserve original sealed
writers/legacy budgets and test readiness ownership, partial bit/line/CLOSE,
noise, suffix safety and cleanup. This leaf changes neither side of that channel.

Root's first proof has a64KiB data-only ingress cap. The saved public host test
observed1672 DLNET return bytes, whose SPACE/LF expansion is at most40128 bytes;
this fits that capture, **not every permitted64KiB HTTP response**. Freeze an
explicit later cap and corresponding frame/HTTP budgets before an egress boot,
or fail on cap exhaustion. Do not quietly raise the old mode's byte allowance.

Later admission needs the new guest/host/profile artifacts bound together, real
Darwin DNS request/reply and socket HTTP bytes matching Linux packet transcripts,
selected public endpoint from the correlated DNS reply, actual full request and
response/FIN, all waits/routes/interface/utun/console cleanup and mandatory verified
Linux recovery. A host-only public result is useful prerequisite evidence; it
does not close Darwin networking, physical NIC, Wi-Fi, GPU or performance work.
