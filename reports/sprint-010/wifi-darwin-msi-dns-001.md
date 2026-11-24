# Wi-Fi: MSI, name resolution and logging cleanup (2026-11-09)

Boots `pcie-enum-candidate-k01` to `-k03` on the J293 under the m1n1 hypervisor; the M1 returned
to Linux after each. Console: `runs/m1-desktop-010/pcie-enum-attempt-k03/` (private).

MSI:

    PDBwfm: msi: capability at 0x58 control 0x008a -> 0x008b, doorbell 0xfffff000, AIC vector 704, enabled
    PDBwfm: first MSI interrupt delivered

The driver programs the device's MSI capability for one message and takes it as AIC interrupt
704 (the ADT's `msi-vector-offset`) through `PDAppleAIC`. The handler is still polled every
250 ms as a safety net (2 ms if MSI cannot be set up). Ping to the gateway went from
220/66/39 ms with 2 ms polling to roughly 5-22 ms; 1.1.1.1 is 16-29 ms.

Name resolution:

    pd-net-up: resolver ready
    PING example.com (172.66.147.243): 56 data bytes
    2 packets transmitted, 2 packets received, 0.0% packet loss

The earlier failure was only that nothing was running the resolver daemon libc talks to.
`/usr/bin/pd-net-up` (`integrations/puredarwin/m1-bwfm/dhcp/pd-net-up`) now does DHCP and then
starts `mdnsd -debug`, which reads `/etc/resolv.conf` itself. The daemon log shows the A and
AAAA queries answered by the DHCP nameserver over `en0`.

Logging: the per-message sleep, the step-by-step chip and attach prints, the per-second port
status thread and the interface-naming diagnostics are removed. A few first-occurrence lines on
the data path remain. `PD_BWFM_SLOW_LOG` re-enables the sleep when chasing a hard stop.

Also fixed: `pd-dhcp` passes the real broadcast address to `netsetup` (it defaulted to 10.0.2.255).

Not done: `netcat` by name prints nothing in this RAM root even with `-4` (HTTP by address and
ping by name both work; not investigated). No DHCP renewal. Link-up is still done by the
launcher at 2.5 GT/s. Nothing starts `pd-net-up` automatically.
