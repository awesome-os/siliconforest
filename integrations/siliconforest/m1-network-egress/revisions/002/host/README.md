# Public backend002: explicit ACK-paced profile

Selected only by `--profile example-http-ack-v2`. Frozen public001 and controlled
backends are unchanged. `policy.py` is an exact byte copy of public001: fixed
example.com A query, reply correlation/CNAME/global-address/TTL rules, first A
only, one TCP80 flow, exact186-byte request, HTTP200/content/framing/FIN/EOF
success and all packet/queue/byte budgets remain intact. No DNS/HTTP endpoint,
host listener, route, firewall, SSH or target behavior is broadened.

The new profile explicitly records `close_eof_seconds=15` and
`active_idle_seconds=30` in ready/result. Idle does not preempt the explicit
closing window; whole configured active time is still checked first and capped
at120s. Root's planned real profile uses backend90, guest/helper75 and external
controller180; those guest/controller bindings belong to root. This source's
nominal host fixture uses the **unchanged production004 helper60** and a55s
synthetic-client deadline. DNS TTL<=60 remains an independent narrower bound.

```
python3 -I backend.py --profile example-http-ack-v2 --nonce HEX48 \
  --codec-library /ABS/FROZEN/libdlnet-host.so --codec-sha256 HEX64 \
  --records /ABS/NEW-PRIVATE-DIR --listen-port 0 \
  --accept-seconds 300 --seconds 90
```

The listener is Linux127.0.0.1 only. Atomic `ready.json` identifies the selected
profile, nonce, port, DNS endpoint, initial unresolved TCP endpoint, deadlines,
actual QEMU/libslirp and codec hashes. No old profile fallback exists. Root owns
the later DL27 console wrapper/typed opt-in, native clients and two sequential
utuns; this backend consumes the same DLNET001 packet stream across that change.

The real host test uses production004 Bridge SHA3213bd2f, the unchanged C codec,
selected QEMU/slirp, synthetic guest packet/whitespace parser and cumulative
ACKs paced at0.1s. It sends the exact MSS256 SYN option and records observed
return sizes. **MSS256 did not cap this QEMU path:** actual TCP payload reached
931 bytes (IP971), requiring20212 whitespace bytes for its largest DLNET frame.
That is106 chunks, about10.6s at0.1s/chunk; the preliminary idle10 test failed
with both DNS and HTTP200 observed. Its complete failure records/raw bytes stay
under `validation/failed-idle10/`, with no clean-link claim.

The source-bound idle30 test then completed actual correlated DNS to
104.20.23.154:80, HTTP200/931 response bytes/342 headers/577 HTML body bytes,
server FIN, client FIN ACK, both DLNET CLOSEs and full serial credit in28.734s.
It consumed52258 whitespace bytes,9 frames,278 chunks/ACKs; simulated1024-byte
TTY high-water was192, no partial bits/line, QEMU returned0/reaped with no KILL
or cleanup error. `mss_bound_observed=false` is retained. No synthetic controlled
echo RESULT marker is injected; host success combines real DNS/HTTP/process
facts with actual production-helper transport completion and synthetic guest ACKs.

Maximum1556-byte DLNET frame expands to at most37344 SPACE/LF bytes,195 chunks,
about19.5s at0.1s/chunk. Idle30 covers that nominal per-frame drain with margin;
this is arithmetic, not a hardware latency guarantee. Whole deadlines,128-frame
and131072-whitespace-byte helper caps remain real failure gates, not throughput
guarantees. Root's guest HTTP cap2048 is stricter than the unchanged backend64KiB
parser and must fail on excess. Preserve actual observed packet sizes and all
raw bytes; do not substitute MSS readback for evidence.

```
python3 -I test_policy.py -v
python3 -I test_backend.py --library /ABS/FROZEN/libdlnet-host.so --output /ABS/NEW-NEGATIVES
python3 -I test_profile.py --library /ABS/FROZEN/libdlnet-host.so
python3 -I test_idle_bound.py --library /ABS/FROZEN/libdlnet-host.so --output /ABS/NEW-IDLE
python3 -I test_public.py --library /ABS/FROZEN/libdlnet-host.so \
  --output /ABS/NEW-PUBLIC --allow-public-example-http
```

11 policy tests,6 real-QEMU refusal/cleanup cases and3 profile refusals pass.
The actual idle-negative expires after30.029s with only the two initial ARPs,
exit1 and QEMU wait0/reaped. All tests are Linux host-only; the public fixture
does make the explicitly allowed read-only example.com request. No M1/M5,
Darwin, SSH, native utun, hardware, physical NIC/GPU or TLS/DNSSEC identity claim.
Root must build/bind/test/qualify the real guest pipeline separately.
