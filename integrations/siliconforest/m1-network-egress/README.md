# Separate example.com egress profile 001

Original Linux-only DNS/HTTP policy and bounded DLNET001/QEMU backend. The
controlled-echo transport001 remains byte-for-byte frozen and keeps its original
destination policy. This opt-in profile is a separate executable, listener and
session; nothing installs or enables it on an existing path.

One actual local-host test sent synthetic guest IPv4 through the real C codec,
loopback relay and QEMU/slirp DNS10.0.2.3, selected the first correlated global
`example.com` A answer, then performed one public TCP80 `GET /`. It received
HTTP200, checked the actual chunked HTML and TCP closure, and reaped QEMU.
Raw packets, DNS/HTTP bytes and hashes are retained in `validation/`. This is
**Linux backend public egress**, not Darwin/M1 egress, Wi-Fi or authenticated
TLS/DNSSEC server identity. No SSH, target, route or firewall change occurs here.

Build the unchanged transport001 host codec into an owned output directory as
documented there; supply its actual digest explicitly. Start only by this CLI:

```
python3 -I host/backend.py --profile example-http-v1 --nonce HEX48 \
  --codec-library /ABS/OWNED/libdlnet-host.so --codec-sha256 HEX64 \
  --records /ABS/NEW-PRIVATE-DIR --listen-port 0 \
  --accept-seconds 300 --seconds 60
```

`ready.json` atomically publishes the loopback-only relay port, nonce, fixed
profile, deadlines and actual QEMU/libslirp/library hashes after both gateway
and DNS-alias ARP checks. DNS authorization begins unresolved. There are no
echo-service listeners and no arbitrary host listener, destination, redirect,
port, method or URL options. See [CONTRACT.md](CONTRACT.md) for byte decisions
and [GUEST-NEXT.md](GUEST-NEXT.md) for the later source/SDK/route/daemon work.

Offline policy and real local-QEMU refusal checks:

```
python3 -I host/test_policy.py -v
python3 -I host/test_backend.py --library /ABS/OWNED/libdlnet-host.so \
  --output /ABS/NEW-NEGATIVE-TEST-DIR
```

The public test is separately and explicitly opted in. It makes one read-only
request to one DNS-selected address; no fallback address or endpoint scan:

```
python3 -I host/test_public.py --library /ABS/OWNED/libdlnet-host.so \
  --output /ABS/NEW-PUBLIC-TEST-DIR --allow-public-example-http
```

The 11 byte-policy tests cover DNS transaction/question/source-port/flags/
compression/CNAME/TTL/global-address cases, endpoint exclusion, exact request
segmentation/retransmission, HTTP smuggling/framing and response reassembly.
Six actual QEMU cases fail closed before public traffic: pre-DNS TCP, wrong
question, premature CLOSE/EOF, accept timeout and SIGTERM. Only their two initial
ARP frames reach QEMU; that is checked from the raw Ethernet transcript.

New policy/test code is original. The event-loop/cleanup code is adapted from
this agent's original frozen transport001 backend; the exact diff and receipt
are retained. Its C codec and Python opaque-state binding are loaded read-only
after checking all14 source hashes against the fixed receipt. No GPL code is
copied; installed QEMU/libslirp are external executables/libraries, not vendored
or relicensed. Root's `m1-network-link` and all earlier networking files stay
unchanged. No GPU observer boot is attempted; the user hold remains in force.
