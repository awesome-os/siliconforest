# Source facts and original implementation boundary

Reference: acquired Omacom/m1n1 Git pin
`121fcbf35d4501814b58ca5347926a4173b6a512`, MIT-marked files below. Exact whole-file
hashes, Git byte comparisons, and existing resolver004 Python snapshot byte
comparisons are retained in validation.json. No GPL implementation was used or
copied; the C codec, tests, fixtures, and checker are original. Original license
selection remains pending. No Python reference module was imported/executed.

| Original function/artifact | Pinned source evidence | Limit |
|---|---|---|
| `ace_decode_rx` | `proxyclient/m1n1/hw/asc.py:24-32` defines INBOX EP[7:0], OUTBOX EP plus four numeric fields; `:54-60` reads OUTBOX0 then OUTBOX1 and wraps metadata as R_INBOX1 | Declared metadata fields only. Missing fields are opaque; no reserved mask or physical coherence proof. |
| `ace_rtkit_input` | `src/asc.h:8-11` stores msg1 as u32; `src/asc.c:226-240` casts legacy read64 metadata to u32; `src/rtkit.c:448-467` skips msg1>=0x100 and narrows accepted endpoint to u8 | Conservative C caller policy, not a silicon rule. Python `hw/asc.py:101-107` routes on EP only. |
| `ace_encode_tx` | `proxyclient/m1n1/fw/asc/base.py:14-15,49-50`: ASCMessage1 EP and fresh endpoint wrapper; `proxyclient/m1n1/utils.py:323-339`: absent initial value starts at zero; `src/rtkit.c:198` sets msg1 to endpoint | Endpoint-only TX encoding pattern; not an acknowledgement or permission to send word0. |
| Canonical byte order and span/error contract | Original serialization and defensive API choice over two numeric u64 words | Not an upstream byte-stream framing declaration or MMIO instruction sequence. |
| Explicit fake commit composition | Frozen `m1-gpu-rtkit-control/control.{c,h}` and receipt, separately reverified | Synthesized delivery labels have no hardware authority. Existing control state machine remains unchanged. |
| Applicability to pinned AGX source path | `proxyclient/m1n1/agx/__init__.py:61` constructs AGXASC; `fw/agx/__init__.py:65-73` subclasses StandardASC; `fw/asc/__init__.py:11,18` imports/subclasses hw.ASC | Code lineage, not runtime T8103 register observation or loaded GPU ABI detection. |

Control-register flags are separately declared in `hw/asc.py:5-12`: ENABLE0,
FULL16, EMPTY17, OVERFLOW18, FIFO count23:20, read/write pointers15:12/11:8.
They are not word1 flags. The reference register offsets `:34-44` distinguish
control, INBOX0/1 and OUTBOX0/1. This codec deliberately takes no control value
and cannot establish availability or send acceptance from any metadata bit.

Transport ordering is not resolved by this source slice: Python ASC.send writes
word0/word1 then waits on FULL (`hw/asc.py:64-76`); the C legacy path waits for
not-FULL, calls dma_wmb, then writes both words (`src/asc.c:263-292`). C receive
uses dma_rmb after capture. Merely encoding the same numbers implements neither
transport. ASCWrap-v8 code in the same pin has different apertures and paired
operations; it is explicitly outside the selected layout and API.

No cached Linux declared firmware ABI, debug size, or RTKit log was used to
select this source-layout codec or to promote Darwin loaded ABI from UNKNOWN.
No firmware words or private identifying runtime data appear in these fixtures.
