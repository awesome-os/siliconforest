# Linux controlled-link backend 001

No command here starts a target, SSH, route, firewall rule or host network device.
The backend listens only on Linux127.0.0.1; it accepts one nonce-bound relay
connection. Build the host library locally into a separately owned directory:

```
clang -std=c11 -Wall -Wextra -Werror -Wpedantic -O2 -fPIC -shared \
  codec/transport.c host/codec_host.c -o /ABS/OWNED/libdlnet-host.so
PYTHONDONTWRITEBYTECODE=1 python3 -I host/backend.py \
  --nonce HEX48 --codec-library /ABS/OWNED/libdlnet-host.so \
  --records /ABS/NEW-PRIVATE-DIR \
  --listen-port 0 --tcp-echo-port 0 --udp-echo-port 0 \
  --accept-seconds 300 --seconds 60
```

All arguments are required. Nonce is fixed before process launch,48 lowercase
hex, never adopted from a connecting peer. Port0 selects an owned ephemeral
port. Records must be an absolute **new** directory; it is created0700 and all
records0600. `ready.json` appears atomically only after actual QEMU gateway ARP
success, with `listen_host`, `listen_port`, `tcp_port`, `udp_port`, nonce,
budgets, PID and artifact hashes. Its existence does not certify a later run.
Waiting for one relay is bounded by1..300s; active connection by1..120s, usually60.

The relay sends guest-to-host binary DLNET001 frames (direction1); backend replies
with direction2. Backend first expects OPEN and replies OPEN. The guest IP is
fixed10.0.2.15, destination only10.0.2.2 and the two selected echo ports. TCP must
carry exactly `b'DL26-TCP '+nonce.encode()`, UDP exactly `b'DL26-UDP '+nonce.encode()`.
The echo servers permit each nonce payload once and preserve observed results.
No DNS3, external IP, IPv4 options/fragmentation, IPv6, host-listener forwarding or
arbitrary destination is enabled in this revision. Real guest BSD socket clients
and the utun pump belong to the separate root-owned `m1-network-link` leaf.

After both actual echoes the guest sends CLOSE. Backend drains accepted work,
sends its CLOSE; the relay must half-close its TCP write side (or close after
draining the reply). Backend requires validated peer EOF, both echoes and clean
QEMU wait/reap before `complete=true` and process exit0. No raw fd3/socket or PID
is accepted as an application command. It opens no public or reconnect listener.

`result.json` is atomically published after resource cleanup. It contains the
first sticky failure, exact nonce, packet/frame counts, payload bytes, actual
QEMU wait status and closed transcript lengths/SHA256. Records:

- `relay-rx.bin` / `relay-tx.bin`: exact received/encoded binary DLNET001 stream.
- `ethernet-rx.bin` / `ethernet-tx.bin`: QEMU's big-endian32 length + Ethernet.
- `qemu.log`: preserved process diagnostics; normal hub/no-NIC and TERM lines
  remain. No string filtering obtains success.
- `ready.json`, `result.json`: endpoints and closed result; private host records,
  not guest or external-network proof.

Queues hold at most8 frames, each<=1556; trial limits4096 frames and4MiB payload
per direction, each transcript<=5MiB, QEMU diagnostics<=16KiB retained through a
bounded nonblocking pipe (overflow is failure, with unretained tail disclosed).
Read work<=4096/iteration, poll<=50ms, continuous nonempty queue/partial frame2s,
active idle10s, close-to-EOF2s. Policy/CRC/session/sequence/partial/broken endpoint
or deadline failures stop the relay and cleanup. SIGPIPE is ignored and EPIPE
remains a failure. SIGTERM/SIGINT reach the same owned cleanup. Only the unreaped
QEMU child may receive TERM, then wait2s/KILL/wait2s if necessary. Forced KILL or
nonzero QEMU status makes the result fail. Parent exit status reflects cleanup.

Run host verification (new output directory only):

```
PYTHONDONTWRITEBYTECODE=1 python3 -I host/test_backend.py \
  --library /ABS/OWNED/libdlnet-host.so --output /ABS/NEW-TEST-DIR
```

This runs actual selected QEMU children and synthetic guest packet fixtures,
plus wrong-nonce/CRC/EOF/partial-timeout/address-policy/accept-timeout/TERM cases.
Every child is bounded and reaped. It also tests actual socket-buffer saturation,
partial stream resume and a broken peer. It does not execute Darwin or establish
external network/DNS. The standalone slirp test additionally demonstrates a
controlled local DNS-shaped reply; that is not a real external DNS answer.
