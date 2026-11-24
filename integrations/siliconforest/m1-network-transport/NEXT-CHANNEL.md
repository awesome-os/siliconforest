# Opt-in utun link and real connectivity: next implementation

This is a concrete channel design and implemented Linux backend, without target
integration. Base network step001 is separate. Root's qualified base run
establishes the guest utun path; this leaf's host tests establish a Linux slirp
backend and actual framed host TCP/UDP roundtrip. They have not been connected
to the M1. Root owns the separate `m1-network-link` guest/controller work.
No installed m1n1, legacy console writer, existing networking leaf, ten-minute
session, route, firewall or hardware setting is changed here.

## Dedicated ownership epoch

A new isolated controller/guest packet mode must be reviewed before a boot.
The existing writer stays sealed after its launcher. The new mode may send one
fixed command that replaces the console shell with the fixed daemon, for example
`exec /usr/bin/darwin-ipv4-tunnel --nonce HEX48 --seconds 45`. No command/config
string, shell escape, HID producer, desktop keyboard scheduler or interactive
writer is reachable in the data mode. The only post-launch writer accepts
complete validated DLNET001 frames, for the bound nonce and direction.

The root-owned daemon exclusively owns the raw console input endpoint; it opens no
interactive shell or child command. Before host TX is enabled it must record the
actual tty identity, controlling/foreground group, saved termios and checked raw
termios readback: no canonical processing, echo, signals, output processing or
flow-control substitutions. It creates utun and emits one bounded, exact readiness
line with nonce, PID, interface, MTU and raw ownership. The controller records
every preceding byte and enables a dedicated logical data channel at that exact
boundary. Root's proposed host ingress wrapper uses **only ASCII SPACE and LF**:
each bit is one SPACE+LF for0 or two SPACEs+LF for1, eight bits per byte,
most-significant bit first. Strictly decode that alphabet into the unchanged
DLNET001 bytes; CLOSE requires no leftover partial bit/line. Every arbitrary
suffix of that wire alphabet is still whitespace, including daemon death after
consuming a prefix; it cannot spell a shell command or HID action. No TAB,
control code, terminal escape, shell text or binary byte is transmitted after
the fixed launcher. Root must separately review that exact writer and parser;
this leaf implements neither. The opt-in first-trial ingress cap is64KiB; one
maximum1556-byte frame expands to at most37344 bytes, so this is a small proof,
not a throughput claim. Bootstrap/launcher accounting stays separate and legacy
writer limits stay unchanged.

Guest output is `DL26_TX_<nonce> <hex>\n`. Hex length is even and at most3112
digits; cap complete and pending partial lines at4096 bytes. Only exact prefix/
session/shape passes to the codec. No CR/whitespace normalization inside hex.
Unrelated **complete** kernel lines may be preserved as diagnostics outside
frame lines; malformed frame lines, a foreign frame nonce, inserted bytes inside
a frame line or an overlong/unterminated line fail sticky. This outer text
demultiplexing does not resynchronize a corrupt DLNET001 stream. Retain every
raw console byte so skipped diagnostic lines cannot erase a failure.
First guest OPEN proves the same source-bound daemon is in the framed epoch;
the host sends OPEN only after both readiness and guest OPEN validate.

**Admission requirement for that future packet:** only a reviewed fixed daemon
may accept data mode, and the post-launch writer must be closed under arbitrary
suffixes as above. A comment+hex prefix alone was rejected: reader change after
prefix consumption can expose a hex shell word. If any future transport expands
the ingress alphabet beyond SPACE/LF, require an independently proven no-shell
fallback/drain owner before enabling it. Malformed input, daemon death or
ownership ambiguity fails the trial even though trailing whitespace stays inert.
Normal completion has the host stop all producers, drain its bounded TX,
exchange CLOSE, seal the writer permanently and close the Linux relay write side
after draining replies. There is no return-to-console command in this mode.
Premature daemon death, missing CLOSE or ownership ambiguity fails the trial.
Kernel/daemon diagnostics must remain outside complete framed lines and their
raw bytes are retained. A distinct raw console device/channel would be preferable
only after its actual guest/host availability is demonstrated. None is invented.

## Exact guest and host endpoints

The future guest daemon opens `PF_SYSTEM/SOCK_DGRAM/SYSPROTO_CONTROL`, resolves
`com.apple.net.utun_control`, connects with kernel-selected unit, and checks
`UTUN_OPT_IFNAME`. Configure its owned interface to 10.0.2.15/32 with peer 10.0.2.2,
MTU1500 and UP. Read/write utun's four-byte network-order AF_INET prefix locally;
that prefix is not sent as part of the IPv4 payload. Reject every other family.
Only the daemon's own interface and explicitly recorded guest routes may change.
For external destinations a guest route through this utun is required: record
the original table, refuse conflicts and remove only the exact route it created.
Do not overwrite an existing default route. Closing the utun control FD removes
the interface. This is no physical NIC or Wi-Fi design.

On Linux, implemented `host/backend.py` owns an AF_UNIX socketpair to this backend
(FD is an already-owned selected descriptor, not a port discovered by name):

```
/usr/bin/qemu-system-aarch64 -machine none -m 32M -nodefaults -no-user-config \
  -display none -serial none -monitor none -run-with exit-with-parent=on \
  -netdev user,id=egress,net=10.0.2.0/24,host=10.0.2.2,dns=10.0.2.3,ipv6=off \
  -netdev socket,id=wire,fd=FD \
  -netdev hubport,id=to-user,hubid=0,netdev=egress \
  -netdev hubport,id=to-wire,hubid=0,netdev=wire
```

No VM CPU, guest disk, monitor, TAP, host route, IP forwarding or firewall/NAT
rule is needed. QEMU/libslirp owns ordinary host TCP/UDP sockets. The installed
QEMU11.1.0 accepted this and delivered real controlled loopback TCP/UDP traffic
in `host/test_slirp_backend.py`; the warning `hub 0 with no nics` is preserved.
It does not mean the socket/hub/user backend is inactive; byte evidence is the
test. Pin the executable and linked libslirp for the later packet, not its name.

The bridge adds Ethernet (static local MAC52:54:00:12:34:56) and QEMU's four-byte
big-endian Ethernet length, distinct from DLNET001. Resolve only gateway10.0.2.2
with bounded ARP; validate hardware/protocol lengths, addresses and operation.
Answer requests only for owned10.0.2.15. Require Ethernet14..1514 bytes; accept
only ARP or IPv4 with exact bounded IPv4 total length and checked padding.
Strip Ethernet before framing return IPv4. No DHCP, broadcast relay, arbitrary
MAC learning or Ethernet-over-console is needed.

M5 can later be an opaque bounded relay to Linux through one explicit SSH
loopback forward (Linux-side bridge listens127.0.0.1 on a selected private port).
This requires separate root-owned SSH/channel review; no forwarding is started
here. Linux remains the host socket/egress endpoint. Bind the forward to one
session nonce and one connection, with no reconnect or listener on wildcard IPs.
SSH encryption authenticates the relay; CRC32C itself does not. No host route or
firewall mutation is planned or authorized by this design.

## Bounded event loop and cleanup

Use checked NONBLOCK and CLOEXEC readback on all owned endpoints. Poll the
owned sources with at most4096 bytes per read/iteration. Each direction has at most eight
1556-byte frames queued (12448 bytes), and a whole-trial limit of4096 frames and
4MiB packet bytes per direction. Stop source polling when its destination queue
is full; never silently drop an already accepted packet. Queue overflow,
partial/broken endpoints, failed datagram injection or budget excess fails with
exact counts. utun writes preserve one complete datagram; short writes fail.
Stream writes resume after partial writes; EAGAIN never becomes success.

The Linux backend uses monotonic readiness5s, partial-frame2s, nonempty-write2s,
active idle10s and whole active session60s deadlines, with polling at most50ms.
Before accepting the single relay, a separately recorded accept budget<=300s
allows root to assemble and boot the packet. Guest45s is separately owned by
root's daemon. Catch/ignore
SIGPIPE and record EPIPE. EOF without validated CLOSE, bad CRC, foreign nonce,
sequence gap and callback refusal are sticky failures; never resync. Record
last sequence, exact accepted/forwarded/queued counts and the first error. Each
direction closes independently; peer CLOSE stops new packets, drains permitted
owned work, and leads to the local CLOSE. Abnormal shutdown seals host TX first.

The Linux parent owns the positive QEMU PID immediately on creation. On all
outcomes close the inherited child endpoint locally, seal/close producers and
relay, TERM only the unreaped owned child, wait2s, KILL if still owned, wait2s,
and retain real wait status. Never signal after cached reap or use kill-0 as an
ownership gate. Join owned test/listener workers. Guest closes its route/socket/
utun/tty FDs and writes closed private results with every cleanup failure;
external controller hard deadline and verified Linux recovery remain mandatory.

## Next admission ladder and byte evidence

1. Source/SDK and host review: finish actual guest daemon, controller epoch and
   bounded records; review this implemented Linux bridge. Test raw tty no-echo, ownership
   loss, saturation/partial/broken pipes, sequence/CRC failures and TERM/KILL.
   Prove readiness ownership and arbitrary-suffix safety before data-mode admission.
2. One later root-owned M1 trial: guest-origin BSD TCP connection to10.0.2.2
   at a selected unprivileged Linux nonce-echo port, and guest UDP nonce echo
   to another controlled host port. Linux-side exact nonce request/reply,
   utun packets, both framed stream directions and guest socket results must
   agree byte-for-byte. The host-only test already exercises these backend
   operations, but is not the Darwin half or cross-machine proof.
3. External connectivity is a separate required next target. Query a controlled
   DNS name through10.0.2.3:53 (slirp DNS), validate query ID/name/type and actual
   response, then originate guest TCP to the approved controlled external IP/port
   and exchange a fresh nonce. Pin expected endpoint/answer policy before boot;
   preserve server-side receipt and DNS packets. Do not hardcode a canned DNS
   answer as external proof or count host-local `.invalid` answers as it.
   The current bridge permits only guest source10.0.2.15 and the two initial
   host test ports; it rejects options and fragments. A new reviewed extension
   must add DNS3:53 and the recorded external TCP endpoint before that trial.
4. Export closed guest/host records with nonce, source/artifact/provider hashes,
   raw bounded frame/IPv4 transcripts and SHA256, sequence/byte/route/interface
   accounting, actual TCP/UDP response bytes, all waits/cleanup and recovery.
   A loopback/feth pass, socketpair roundtrip, OPEN exchange or backend process
   survival cannot qualify external Darwin networking.

DNS resolver configuration, general persistent networking, throughput, physical
NICs and Wi-Fi remain later engineering. GPU work is separate and on user boot
hold. None of these source or local-host tests executes the M1 or M5.
