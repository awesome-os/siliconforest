# Wi-Fi M4: BCM4378 firmware running and scanning under Darwin on the M1 (2026-11-09)

Boot `pcie-enum-candidate-w12` on the J293 under the m1n1 hypervisor (twelve Wi-Fi boots,
w01-w12; the M1 returned to Linux after each). Console: `runs/m1-desktop-010/pcie-enum-attempt-w12/`
(private; it contains neighbouring network names).

Observed on the serial console:

- `PDBwfm` (OpenBSD bwfm port, `integrations/puredarwin/m1-bwfm/`) attaches to `14e4:4425`,
  chip 4378 rev 3, OTP module `RASP` vendor `m` version `6.11`.
- Firmware `brcmfmac4378b1-pcie.apple,honshu.bin` (1377067 bytes) and NVRAM `...-RASP-m.txt`
  download; firmware up after 350 ms, shared structure version 7.
- DMA through `dart-apcie0` works: the chip consumes control and receive-buffer rings and
  writes completions into the bounce pool; no DART fault.
- First control message answered; MAC `74:8f:3c:xx:xx:xx` (same as under Linux); CLM, TxCap
  and calibration blobs loaded.
- `en0` exists; `wifiutil info` reports power on and the address; `wifiutil scan` lists
  about 27 access points on 2.4 and 5 GHz with plausible RSSI.

Fixes found on hardware, in order:

1. Apple PCIe windows must be mapped posted-write (Device-nGnRE, `kIOMapPostedWrite`). With
   the default non-posted device mapping, reads work and the first BAR write raises an SError.
2. The packet-id table is smaller than OpenBSD's; posting data buffers first left no ids for
   command-reply and event buffers, so the firmware answered the first command with a general
   status error. Control buffers are now posted first.
3. Nothing names network interfaces in the RAM root (no configd). The driver asks
   `IONetworkStack` to register all interfaces, which yields `en0`.
4. A window/register ordering guard (write the BAR0 window, read it back) was added while
   chasing (1); it follows brcmfmac and is kept.

Still open: association, WPA2, IP traffic (M5). Interrupts are polled every 2 ms; the link is
brought up by the launcher at 2.5 GT/s; every driver message is followed by a 25 ms sleep
(bring-up aid) and the bridge logs port status once a second for two minutes.
