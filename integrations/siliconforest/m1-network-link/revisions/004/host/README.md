# Revision004 host receive-credit helper

Isolated adaptation of frozen003 `line_bridge.py`. Root owns guest004, the
projected host dispatch/profile and `test_host_integration.py`; this source set
does not edit or freeze those files. Constructor/API entry points stay the same;
root selects the new profile tag `dlnet001_space_lf_ack_v2` separately. Legacy
writers, guest001/002/003 and all earlier packets remain unchanged.

Only one issued chunk is outstanding. `tick()` offers at most192 SPACE/LF bytes
from the current whole frame at its retained offset. `authorize(data)` must match
that exact immutable byte slice and current credit before the existing recorded
writer writes. It records length/SHA256/cumulative bytes, advances the offset,
and starts a fixed3s outstanding-ACK deadline. A repeated authorization without
credit is refused. The original full frame and receipt remain until its last
chunk is authorized; only then is the original receipt appended exactly once to
`issued` / `host_frames_issued`. Authorization is an issued-byte fact, not proof
of guest receipt.

The guest must emit exactly `DL26_LINK_ACK_<nonce> bytes=N` after an actual
successful console read/parse. Strict cumulative increases must satisfy
previousACK<N<=data_bytes. Partial ACKs are recorded but grant no new chunk until
N==data_bytes, and do not reset the fixed deadline. Reject before-issued,
duplicate, backwards, too-large, zero, malformed, foreign-nonce or late ACKs.
Caps are1024 chunks,4096 ACK records,131072 issued bytes and the inherited60s
whole-link deadline. ACKs are acceptance by the bound guest parser; they do not
claim external networking or physical input.

All data is still SPACE/LF only, so every arbitrary suffix remains shell
whitespace, including a chunk or daemon death in the middle of a bit-line.
The guest retains partial line/bit/byte/frame state between reads/chunks.
Trailing CR normalization from003 is preserved for complete output lines only;
interior CR, nonce/CRC/direction/sequence and line caps are unchanged.

Both codec CLOSEs, drained relay bytes, no pending frame/offset and **all issued
bytes ACKed** are required before SHUT_WR. Relay EOF is valid only after that
shutdown and exact host stream end. Report transport completion includes those
credit conditions. Errors stay sticky, cancel pending sends and invalidate
completion; raw capture is retained by the outer unchanged console owner.
The unchanged backend's2s close/EOF grace can expire before this helper's3s ACK
deadline; that remains a real failure, not an extended or hidden wait.

Guest RESULT now requires the original fields followed exactly by
`console_bytes_in=N partial_line=N partial_bits=N`. Parsed old keys remain.
PASS requires errno0, both echoes, no partial line/bit, all credited issued bytes
equal reported console bytes and no pending frame. The report is version2 and
adds `acked_bytes`, `credit_complete`, `chunk_ledger`, `ack_ledger`, caps and
pending state. Final root qualification still requires both CLOSEs, actual
guest/process/backend outcomes and recovery; a marker alone is not success.

Run source tests in this directory:

```
python3 -I test_line_bridge.py
python3 -I test_bridge_backend.py --library /ABS/FROZEN/libdlnet-host.so \
  --output /ABS/NEW-PRIVATE-TEST-DIR
```

15 unit/simulation tests cover the1024-byte tty capacity model (the old whole
OPEN exceeds it, credited chunks peak at192), partial/fake ACKs, fixed deadlines,
caps, exact authorization/receipts, CLOSE/EOF, strict results,003 CR and H1, and
every chunk suffix. The actual Linux backend test uses this production helper,
the unchanged real C codec and selected QEMU/slirp controlled TCP/UDP echoes.
Synthetic guest reads preserve bit/line state and emit partial/full ACKs only
after successful C parsing. Recorded run:11178 bytes fully ACKed,6 frames,
61 chunks,118 ACKs, simulated tty high-water192, QEMU wait0/reaped.
These are host tests with synthetic guest ACK/IP/result, not Darwin console,
utun, hardware, SSH, public networking or GPU execution.
