# Networking step 001: guest network stack, loopback, feth pair and utun

First concrete networking step for the PureDarwin M1 guest. It needs no NIC,
no firmware, no Wi-Fi and no macOS kext. It runs entirely inside the guest on
the native KC the M1 already boots, and it creates only virtual interfaces,
which it destroys before exiting.

Status: source, host tests and an arm64 build are ready. Nothing has been
staged, delivered or booted. Packaging and the boot belong to Codex (root),
under the usual gates and with verified Linux recovery.

## Why this step

- The booted kernel (DEBUG_ARM64_T8103, config MASTER.arm64) already includes
  INET, BPF, Skywalk (`options SKYWALK`, with kernel-pipe, user-pipe,
  flowswitch and netif nexus support), `utun` (`com.apple.net.utun_control`)
  and `feth` (`if_fake`). The kernel binary carries the matching strings.
- The RAM root has `/bin/sh`, `/bin/sysctl` and `/bin/ping`, but no `ifconfig`.
  The probe therefore does its own ioctls. Every import (44) resolves in
  libSystem 9fc03c87 and in canonical 8ff49e0c.
- Apple's Wi-Fi driver and its firmware are not used. The M1 has no
  physical NIC that works under this guest today.

## What the probe does (`/usr/bin/darwin-net-probe --nonce <48 hex> [--skywalk]`)

Each step prints one line: `DL25_NET <nonce> <step> <pass|fail|skip|info> k=v...`.

1. Identity (uname, euid), a few read-only sysctls
   (`net.link.fake.bsd_mode`, `net.link.fake.nxattach`,
   `kern.skywalk.priv_check`, `net.inet.ip.forwarding`), and the interface
   list before and after.
2. `lo0`: add 127.0.0.1/8 and set UP only if missing; then UDP and TCP
   loopback with a nonce-tagged payload and an echo.
3. A `feth` pair in the default BSD mode: create two clones, peer them with
   `SIOCSDRVSPEC`/`IF_FAKE_S_CMD_SET_PEER`, set both UP, then open one BPF
   device on each:
   - raw frames A to B and B to A (ethertype 0x88b5, nonce tag), each checked
     on the receiving side, plus packet-counter deltas;
   - assign 10.250.0.1/24 to A and send UDP to 10.250.0.2:9. The kernel's ARP
     request must arrive on B. The probe answers it through B's BPF, and the
     queued IPv4/UDP datagram must then arrive on B byte for byte;
   - destroy both interfaces.
4. `utun`: open through the kernel control socket (the kernel picks the unit),
   configure 10.251.0.1 to 10.251.0.2, UP. A UDP send to the peer must appear on
   the utun descriptor, and an IPv4/UDP packet written to the descriptor must
   reach a local UDP socket. Closing the descriptor destroys the interface.
5. Optional `--skywalk`: set `net.link.fake.bsd_mode=0`, repeat step 3 on a new
   pair (10.252.0.0/24), so the pair attaches as native Skywalk netif
   interfaces, then restore the previous value.

Every wait is at most 2 s, so the whole run takes seconds. Exit 0 means no
required step failed; 1 means one did; 2 is a usage error.

What a pass establishes: the guest's BSD stack, ARP and IPv4 output, BPF, the
feth software link and the utun user-space packet path work on the real M1.
With `--skywalk`, the same holds for Skywalk native netif interfaces. It does
**not** establish any physical network, external reachability, DHCP, DNS,
Wi-Fi, USB networking or performance.

## Exact files

| Path | Role |
|---|---|
| `integrations/puredarwin/m1-network/STEP-001.md` | This hand-off |
| `integrations/puredarwin/m1-network/probe/darwin-net-probe.c` | Guest probe (Darwin only) |
| `integrations/puredarwin/m1-network/probe/net_wire.c` | Portable frame, ARP, IPv4/UDP and BPF-walk helpers |
| `integrations/puredarwin/m1-network/probe/net_wire.h` | Their interface (BPF layout pinned by `_Static_assert` in the probe) |
| `integrations/puredarwin/m1-network/probe/test_net_wire.c` | Host tests for the helpers (ASan/UBSan) |
| `integrations/puredarwin/m1-network/supervisor/darwin-net-probe-test` | Guest launcher script (`<nonce> [skywalk]`) |
| `integrations/puredarwin/m1-network/decode/decode_net.py` | Strict host decoder for a console capture |
| `integrations/puredarwin/m1-network/decode/test_decode_net.py` | Decoder host tests |
| `integrations/puredarwin/m1-network/build-net-probe.nix` | arm64 build recipe (same toolchain/SDK/link shape as `build-records.nix`) |
| `integrations/puredarwin/m1-network/CRITERIA-DRAFT-001.json` | Proposed criteria; root fixes the final copy before any boot |
| `integrations/puredarwin/m1-network/source-validation.json` | Hashes of all of the above, build outputs and test logs |
| `integrations/puredarwin/m1-network/validation/*.log` | Host test and build logs |

Build output (private, git-ignored): `runs/m1-desktop-010/net-probe-build-001/`
(`result` link, `build.log`, `undefined.txt`).

## For Codex: packaging and the boot

1. Build: `nix --extra-experimental-features 'nix-command flakes' build --impure --file
   integrations/puredarwin/m1-network/build-net-probe.nix`. The recorded build
   produced `usr/bin/darwin-net-probe` (36605 B) and
   `usr/bin/darwin-net-probe-test`; their hashes are in `source-validation.json`.
2. RAM root: add exactly two executable members, `/usr/bin/darwin-net-probe`
   and `/usr/bin/darwin-net-probe-test`, to a reviewed RAM root that already
   carries libSystem 9fc03c87 (for example the live001 root, 5ccf81e5). Change
   no other member. The kernel and boot arguments can stay as they are; the
   probe needs no trackpad, keyboard or desktop component.
3. Launcher: the guest must run `/usr/bin/darwin-net-probe-test '<nonce>'` as
   root from the console shell. Today's host sends a fixed desktop launcher;
   whether to add a host mode for this line is root's decision and review.
   Use the plain form for the first boot; add `skywalk` only in a later boot,
   or after the base result is in.
4. Grade the capture with `decode/decode_net.py <console.bin> <nonce>` against
   criteria fixed before boot (start from `CRITERIA-DRAFT-001.json`).

Risk: the probe uses ordinary kernel paths (clone create/destroy, BPF, ARP,
utun). A kernel fault there would end the guest early. The existing controller
recovery to verified Linux covers that, as for any other boot.

## Next steps (not implemented here)

- Step 002, real connectivity without Wi-Fi: m1n1's hypervisor already
  emulates virtio-mmio devices with Python back ends (`hv/virtio.py`,
  `hv_virtio.c`; only a 9P transport exists), attached as
  `/arm-io/virtioN` with `compatible = virtio,mmio`. The KC's `IOVirtIONet`
  matches only `IOPCIDevice`, so this needs either a virtio-mmio transport in
  IOVirtIOFamily or a different bridge. An alternative that needs no guest
  driver is to carry utun packets over a dedicated host channel. That route
  would need a reviewed host change, because the console writer is sealed
  after the launcher.
- Native USB or PCIe networking needs DART, PHY and controller bring-up first,
  and is out of scope until those exist.
