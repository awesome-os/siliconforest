# Wi-Fi M5: WPA2 join, DHCP and internet traffic under Darwin on the M1 (2026-11-09)

Boots `pcie-enum-candidate-j05` and `-j06` on the J293 under the m1n1 hypervisor; both gave the
same result and the M1 returned to Linux after each. Console: `runs/m1-desktop-010/pcie-enum-attempt-j06/`
(private).

Observed on the serial console (j06):

    PDBwfm: state SCAN -> AUTH / AUTH -> ASSOC / ASSOC -> RUN
    PDBwfm: WPA2-PSK handshake by the firmware supplicant
    PDBwfm: firmware supplicant status 6 reason 0
    PD_DHCP_LEASE addr=10.1.1.211 mask=255.255.255.0 router=10.1.1.1 dns=10.1.1.1 lease=86400s
    64 bytes from 10.1.1.1: icmp_seq=0 ttl=64 time=221.878 ms   (3/3 replies)
    64 bytes from 1.1.1.1: icmp_seq=0 ttl=57 time=34.131 ms     (3/3 replies)
    HTTP/1.1 301 Moved Permanently / Server: cloudflare         (GET to 1.1.1.1:80 over TCP)
    wifiutil info: state running, ssid <home network>, channel 40, rssi -39 dBm

So Darwin on the M1 joins a WPA2-PSK network on the built-in BCM4378, takes a DHCP lease, and
passes ICMP and TCP traffic to the LAN and the internet over `en0`.

Pieces added for this step:

- `integrations/puredarwin/m1-bwfm/dhcp/pd-dhcp.c`: a small DHCPv4 client over BPF (no
  renewal) that hands the lease to `/bin/netsetup`. It also marks the interface up, which
  BPF needs before it will transmit.
- Transmit fix: the linearisation helper failed every frame on a packet-header copy that
  transmit does not need; removed.
- `supervisor/darwin-wifi-join-test.in`: the guest test; SSID and passphrase come from
  `private/wifi.env` at assembly and are never committed.

Not working / not done:

- Name resolution: `netcat example.com` fails with "nodename nor servname provided" although
  `/etc/resolv.conf` has the DHCP nameserver; starting `mdnsd` by hand did not change it. This
  is the RAM root's resolver, not the Wi-Fi path.
- No DHCP renewal, no WPA3, no roaming tests, no throughput measurement.
- Interrupts are polled (2 ms); first ping is ~220 ms, later ones 20-65 ms.
- The launcher brings the PCIe link up (2.5 GT/s); still under the hypervisor, one CPU.
- Bring-up logging (25 ms sleep per driver message, per-second port status) is still enabled.
