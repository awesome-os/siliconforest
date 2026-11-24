# Controlled link004: acknowledged serial receive credit

The new explicit transport tag is dlnet001_space_lf_ack_v2. The host issues at
most192 SPACE/LF bytes, then waits for the guest to acknowledge actual parsed
cumulative bytes. Partial acknowledgements do not reset the3s deadline. No next
chunk is authorized until full credit returns. Full-frame receipts remain exact;
chunk and ACK ledgers are bounded. Every data suffix remains shell whitespace.

The guest equals select revision002 plus queued ACK output after successful
console parsing and final byte/partial-line/partial-bit diagnostics. No malformed
input is acknowledged. PASS requires full input accounting, both actual socket
echoes and reciprocal codec CLOSE. The host requires all ACKs before EOF/close.

The original frozen transport/backend, kernel, libSystem9fc, input, RAM payloads,
recovery and installed m1n1 are unchanged. No public routes/DNS/HTTP, GPU observer,
firmware, Feature report, trackpad mode write or host HID enters this candidate.

Offline evidence:15 helper tests,12 integration tests,61 legacy tests;268 actual
C-parser cases/8033 assertions, select adapter and3 detected mutations; production
helper+Ccodec+QEMU controlled echoes with11178 bytes,61 chunks/118 ACKs, simulated
TTY high-water192 and QEMU exit0. These are not Darwin integration claims.
