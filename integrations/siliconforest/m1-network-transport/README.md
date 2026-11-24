# M1 IPv4 transport prerequisite 001

Original portable C framing codec, bounded Linux slirp backend and opt-in
channel/utun plan.
Only this new leaf is changed. No target integration, SDK build, SSH, hardware
access, m1n1 modification or route/firewall changes occur here.

`codec/transport.{h,c}` implements bounded DLNET001 framing. See [PROTOCOL.md](PROTOCOL.md)
for bytes and failure rules; [NEXT-CHANNEL.md](NEXT-CHANNEL.md) defines the exact
ownership, endpoint, cleanup and guest-origin TCP/DNS admission route. The host
backend CLI/build and records are in [host/CLI.md](host/CLI.md). No guest daemon
or Darwin integration is claimed here. CRC32C detects corruption and is not a MAC.

Run host checks with a separately owned temporary output path:

```
clang -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 -g \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  codec/transport.c codec/test_transport.c -o /tmp/OWNED/test_transport
/tmp/OWNED/test_transport
PYTHONDONTWRITEBYTECODE=1 python3 -I host/test_slirp_backend.py
clang -std=c11 -Wall -Wextra -Werror -Wpedantic -O2 -fPIC -shared \
  codec/transport.c host/codec_host.c -o /tmp/OWNED/libdlnet-host.so
PYTHONDONTWRITEBYTECODE=1 python3 -I host/test_backend.py \
  --library /tmp/OWNED/libdlnet-host.so --output /tmp/OWNED/NEW-TEST-DIR
```

The C tests cover every split of a maximum-sized three-frame stream, every
truncated prefix, every single-bit corruption in a data frame, CRC32C's known
vector, limits/session/direction/sequence/header/IPv4 errors, callback refusal,
EOF/CLOSE rules and 128 variable packets each way through actual host sockets.
They establish portable codec behavior only. The Python test starts one finite
Linux QEMU11.1 backend with no emulated machine devices, sends original Ethernet/
IPv4 packet fixtures and observes controlled UDP/DNS-format and TCP nonce echoes
through real libslirp to owned loopback servers. It records packet bytes/digests,
retains diagnostics, and terminates/reaps its child. It sends no external traffic.
It is an explicit local process test, not an automatically started service.
The reusable backend uses the frozen C codec through an opaque allocation shim,
with no C struct layout declared in Python. Its synthetic guest test traverses
actual loopback TCP, framing, bounded Ethernet/ARP and QEMU/slirp to controlled
TCP/UDP nonce echo servers. Eight cases plus actual queue saturation, partial
resume and broken-peer checks establish this Linux prerequisite only. They also
check failure records and actual cleanup. Root's guest/console link is separate.

All code in this leaf was written originally for this task. No GPL implementation
was copied or translated. QEMU is used as an installed executable, not vendored;
this leaf does not relicense it. Project license selection remains pending.
The frozen `m1-network` probe was read as a reference, not edited or copied.
Source provenance, commands, outcomes and hashes are in `source-validation.json`
and `validation/`. No host observation here proves Darwin external networking.
