# Native resolver physical-drain revision 002

This is a new source profile, `native-resolver-example-http-drain-v2`, not a
retry or reinterpretation of attempts 001–003. Those packets, captures, frozen
sources and failed full-trial grades stay unchanged. Kernel/native input,
libSystem9fc, actual inherited libdyld428, probe3568 and mdnsd4b1f stay unchanged.
Only this new parent, the exact typed host/helper and backend004 change.

Attempt003 already proved the native lookup, exact HTTP200/931-byte response,
owned daemon exit4/endpoint cleanup and full route restoration in scope. It did
not drain the complete data protocol. Its real931-byte TCP response was
retransmitted; 30124 SPACE/LF bytes still remained when guest CLOSE was seen.
The helper correctly withheld its relay half-close until physical target credit
and both codec CLOSEs completed. Fifteen seconds expired with3054 bytes still
unconsumed. Empty Linux socket queues were not proof of a drained target.

The new parent allows a fixed40-second close drain inside a105-second whole
run. The backend allows120 seconds active,40 seconds for CLOSE/EOF,30 seconds
idle; the outer M1 capture uses210 seconds. Byte/frame/queue limits,192-byte
credit and its3-second deadline are unchanged. No retransmission is dropped or
fabricated, and no CLOSE/credit/EOF condition is weakened. A separate paced Linux
fixture must demonstrate old15 failure and new40 completion before runtime.

`close_guard` is original pure C state logic. Its production budget is40000 ms;
15000 exists only for the legacy-policy offline comparison. It requires actual
peer codec CLOSE, empty input/output queues (including the final queued ACK),
zero partial line/bits and a valid codec end before the strict deadline. Clock
regression, overflow, invalid/reused state and malformed bounds fail closed.
The parent resamples the clock after potentially lengthy observations; its
previous loop timestamp must not be used for the new closing state.

Four fixed RESULT fields expose the guest clock facts: close_started_ms,
close_deadline_ms, close_completed_ms and finished_ms. They are observations,
not authority; host/backend clocks have separate domains. Existing observation
kinds and all24 frozen native-service/route checks remain unchanged. New
strict host metadata rejects the old profile and validates the new bounds.

Focused host tests exercise93335 guard assertions, the real extracted parent
closing branches, compiled source mutants and the old15/new40 comparison.
Actual ARM64 SDK compilation and import closure are prerequisites, not a boot.
GPU observer hold remains absolute. No human input, firmware, physical driver,
installed m1n1, mount or storage change belongs to this revision.
