# Read-only passive-child admission

`darwin-native-input-inventory --nonce <48 lowercase hex>` makes one finite
registry observation. It never opens either HID endpoint, reads reports, sends
features, changes properties, waits, or retries. Exit0 means identity and copied
ready-state observed, exit77 means clean absence or well-formed non-ready state,
exit1 means error/malformed attribution, and exit2 rejects CLI before any RPC.
Neither exit0 nor node presence proves input, future liveness, queue exclusivity,
multitouch, or physical display scanout.

`main.c` uses the actual tiny IOKit archive and generated MIG declarations. It
preserves raw iterator status: only NoDevice with a null result is EOF. Matching
success plus null is empty; a null parent iterator after observing the expected
pair is an error. Every returned send right, including an error result and a
second reference with the same port name, is owned and released exactly once.
All remaining releases run after any failure; a failed release forbids admission.
The tiny IOMasterPort routine internally obtains its host right; that inherited
routine is unchanged. Its returned main right is explicitly released here.

The walk admits exactly SFM1SPIHID and SFM1SPITrackpad, distinct nonzero64-bit
IDs, no SFSerialHID, and exactly one child parent with the keyboard's ID. It
reads the retained child's110-byte descriptor,4-byte interface and32-byte child
state. The descriptor must equal the acquired private identity guard. The gate
does not discover dynamic property types: the bound kernel code sets OSNumber32;
the byte RPC checks only the value and exact length. Endpoints are checked by
lstat, rejecting symlinks, wrong types and errors other than ENOENT.

The704-byte private record has explicit little-endian offsets in gate.h; no
host C struct layout crosses the wire. It contains actual class/ID/property
observations and failure status. The public JSON summary omits IDs. Eleven
ordered64-byte hex chunks and the footer SHA256 bind the private bytes to the
fresh nonce. The offline decoder requires the entire adjacent export without
interleaved warning filtering, checks the digest and summary types, and rebuilds
the verdict with independent Python policy. Raw captures stay private.

The fixed record is a non-atomic observation, never a service lease. Node
publication is asynchronous; the supervisor waits by stat before this gate and
the actual Both seat must still open both nodes afterward. Finite RPC count is
not a wall-time guarantee; the existing bounded host controller and Linux
recovery remain required. No descriptor Feature02 exists; this gate authorizes
no mode write.

Host checks use explicitly fake operations. They exercise24 query-failure
boundaries,7 release-failure boundaries, ownership with repeated port names,
null/error/invalid/excess cases, all boolean state combinations and signed error
extrema,30000 deterministic C/Python policy comparisons, strict export framing,
and5 compiling assertion-killed mutants. Assertions are required in the C test.
The actual-digest export test needs the private acquired descriptor; it reports
SKIP when that capture is absent. Run `python3 test_gate.py` from any directory.
These checks establish software policy only, not a Darwin runtime result.

Original implementation, license pending. Primary ABI and status behavior are
the pinned SiliconForest tiny IOKit and XNU IOUserClient.cpp described in the parent
REGISTRY_GATE_DESIGN.md. The root build recipe checks all5 immutable inputs;
the generated reference header remains outside Git.
