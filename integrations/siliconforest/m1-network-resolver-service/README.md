# Owned native resolver service trial

This is a new DL29 experiment. It keeps the previously qualified owned default
route and packet bridge alive while the pinned, unmodified `mdnsd -debug` serves
one real libc `getaddrinfo("example.com", "80", AF_INET/SOCK_STREAM)` child. The
parent uses the first address in that API's exact ordered result for the existing
bounded HTTP request. DNS wire order may differ; the independent backend must
correlate the raw answer set and the selected destination. There is no resolver
fallback, fabricated response, or synthetic network success.

The three ARM64 executables are source/build prerequisites until a saved M1
capture proves the lookup, actual DNS/HTTP packets, service lifecycle, route
restoration and a new verified Linux boot. This first owned service is finite
(75 seconds), not yet a persistent desktop network daemon or physical Wi-Fi.
The native kernel, retained libSystem9fc, input and installed m1n1 are unchanged.
The GPU observer remains on hold.

`child_owner` uses the existing reviewed `chonk_darwin_spawn_v1` unchanged, with
CLOEXEC_DEFAULT, checked 0x400e flags and explicit child 0..3 only. Child stdin
and fd3 are `/dev/null`; stdout/stderr are private pipes. These two foreground
executables do not fork. Each PID transfers once even if spawn's local cleanup
fails. Pipe drains are bounded per tick, wait is WNOHANG, lost ECHILD ownership
forbids signals, TERM has a 2-second grace before one KILL, and the caller bounds
final cleanup to 5 seconds. Runtime success requires actual reaping and both
pipe EOFs, not a successful spawn or socket existence alone.

`endpoint_owner` requires exclusive process context, a trusted root-owned RAM
`/var/run`, exact root-owned regular one-link `/etc/resolv.conf`, no
`/etc/mdnsd.conf`, and initially absent UDS/PID paths. It records the daemon's
socket and PID-file identities and checks the PID bytes. Only after that sole
PID is reaped may the parent unlink an exact retained identity. Replaced or
unobserved leftovers are left untouched and fail the trial. The config is read
only. The child environment is fixed and omits DNSSD_UDS_PATH. The daemon may
drop privileges; its intentional TERM normally returns EINTR (4), not zero.
The parent verifies that status, observes cleanup, then closes the network
protocol and retires its owned interface; it does not relabel rc4 as rc0.

`probe_record` accepts only the complete, exact successful serializer-v1 line
for the owned probe PID/nonce. It preserves duplicates and API ordering, bounds
32 addresses, and never accepts a valid prefix, trailing data or failure JSON.
All child bytes and the original raw wait/spawn results remain observations.
Diagnostics are chunked in 64-byte hex pieces, with stdout 8192 bytes, stderr
65536 bytes and a 512 KiB aggregate observation cap. Observation failure is
sticky and cannot authorize HTTP or a pass.

Host tests exercise the actual parser and process owner with real pipes and
fault adapters, real root-owned temporary filesystem/UDS fixtures, and the
extracted service transition bodies. They are source evidence, not Darwin
execution. Mutation tests compile separately; production files are untouched.
