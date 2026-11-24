# DLNET001 framed IPv4 stream

Original allocation-free C codec; no daemon, socket I/O, device access, route,
clock, spawn or host configuration operation. The caller supplies one trusted,
ordered byte stream for each direction, one fresh session nonce and deadlines.
Two decoder states are required for full duplex. The code is single-threaded
and callbacks must not re-enter it. This is an incremental transport prerequisite.

Every integer is unsigned big endian; serialize bytes, never a C struct:

| Offset | Bytes | Value |
|---|---:|---|
| 0 | 8 | ASCII `DLNET001` (protocol version 001) |
| 8 | 1 | type: OPEN=1, IPv4=2, CLOSE=3 |
| 9 | 1 | direction: guest-to-host=1, host-to-guest=2 |
| 10 | 2 | zero, reserved |
| 12 | 2 | payload bytes |
| 14 | 2 | header bytes, exactly 56 |
| 16 | 8 | sequence, starts at 1 independently per direction |
| 24 | 24 | exact session nonce (48 lowercase hex at the CLI boundary) |
| 48 | 4 | CRC32C of header + payload with these four bytes treated as zero |
| 52 | 4 | zero, reserved |
| 56 | 0..1500 | payload |

CRC32C uses reflected polynomial `0x82f63b78`, initial all ones and final
complement. The standard `123456789` result is `e3069283`. It detects accidental
corruption; **it is not authentication**. A nonce prevents accidental cross-run
mixing, not forgery. The local controller/relay and SSH forwarding, if later
used, remain the trust boundary. No exposure of this protocol on a public socket.

OPEN must be first with empty payload, exactly once. IPv4 follows OPEN, with
20..1500 bytes. CLOSE has empty payload and ends that direction irrevocably.
Frames must carry exactly the next sequence: duplicates, gaps, replay, wrong
session/direction and wrap are fatal. At UINT64_MAX only CLOSE is allowed.
OPEN/CLOSE are also integrity checked and delivered to the callback. Application
success requires actual bidirectional packet evidence, not OPEN/CLOSE alone.

IPv4 requires version 4, IHL 20..60 bytes within the packet, total length equal
to payload length and a valid IPv4 header checksum. Options and fragments remain
opaque IPv4 at codec level. A later bridge must implement its narrower endpoint,
fragment, address and port policy separately; no TCP/UDP checksum verification,
IP reassembly or host-stack forwarding is performed by this codec.

Maximum frame size is 1556. The decoder uses one fixed frame buffer, rejects
bad headers and oversized lengths after the first 56 bytes, and never allocates
from advertised sizes. Partial headers/payloads are retained across feeds; many
coalesced frames can be fed together. CRC and IPv4 validation complete before a
packet reaches the callback. Caller buffers passed to the encoder must be
disjoint; state and all spans must remain valid for the synchronous call.

There is **no scanning, filtering, skip-byte rule or automatic resynchronization**.
The first error is sticky. CLOSE followed by any byte is fatal. EOF before CLOSE
or with a partial frame is truncated, even if earlier packets were valid. An
increased but capped corrupt length may wait for further bytes: the caller's
partial-frame deadline or EOF must terminate it. Codec feeds own no clock.
A callback refusal is fatal and never retried. Callbacks for earlier valid frames
remain committed if a later coalesced frame fails. A new decoder requires a new
authorized channel/session; it must not be used to conceal an error in the old one.

TX errors leave its state and output unchanged and report zero written. RX
errors terminate the channel; returning an error does not undo earlier callbacks.
The callback receives a borrowed packet view; copy into an already bounded queue
before returning. A future event loop must cap feed work per iteration, enforce
whole-session/partial-frame/write deadlines and stop polling producers under
backpressure. Those I/O responsibilities are deliberately not claimed here.
