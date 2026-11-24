# Controlled link005: bounded post-CLOSE drain

Candidate004 observed real M1 TCP and UDP echoes, but did not qualify clean
shutdown: guest close3s and backend EOF2s expired while acknowledged serial data
was draining. The failed record remains unchanged.

Guest changes only close_deadline3000 to15000ms. Backend changes only its
post-CLOSE EOF window to15s and prevents the ordinary10s idle window from
preempting that explicit closing window. Whole guest45s, helper60s, backend60s,
chunk192/ACK3s and all packet/CRC/nonce/sequence/echo requirements remain.
A new exact profile requires close_seconds15; older profiles fail that gate.

The production helper is byte-identical to004. Codec, kernel, libSystem9fc,
input, m1n1 and all other RAM payloads remain unchanged. The backend is an
explicit new frozen selection; transport001 is not edited or silently replaced.
No GPU observer, public network or physical NIC enters this controlled test.
The host delayed-close regression rejects old2s and passes new15s, with all
owned backend/QEMU children reaped. SDK compile and runtime admission are distinct.
