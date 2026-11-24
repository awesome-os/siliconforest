## Goal
Take Wi-Fi from "works in a three-minute test" to dependable.

## State (all proven on the M1; `reports/sprint-010/wifi-*.md`, `pcie-*.md`)
Cold start by the kexts alone (radio power, PCIe at 5 GT/s), firmware load, scan, WPA2-PSK join, MSI, per-buffer DMA mapping, DHCP with renewal, name resolution, auto-rejoin (`pd-netd`), radio off/on. Short single-stream TCP runs measured about 190-260 Mbit/s each way.

## Architecture (fixed; do not relitigate)
OpenBSD `bwfm` ported onto `IO80211Family` (`integrations/puredarwin/m1-bwfm/`), Omacom `brcmfmac` as the reference for chip behaviour, firmware supplicant for WPA2, bridge and DART in `m1-apcie-host/`. Kexts are GPL-2.0 as a whole.

## Open defects
1. **Firmware stops answering after the first radio-off.** Seen in boot `l02`: after `wifiutil off` from the associated state and several idle seconds, control messages go unanswered (control ring not consumed). The driver now retries, halts and reloads the firmware, which works, but the cause is unknown. Leading suspect: the firmware's deep-sleep request, which the driver ignores (Linux acknowledges it); `bwfm_dsack=1` enables an untested acknowledgement.
2. `netcat` by name prints nothing in the RAM root though lookups work.
3. The PureDarwin tree patches (`integrations/puredarwin/puredarwin-patches/`) are stale: they predate the SMC client, the mapper, `pd-netd` and the lifecycle work.
4. Bring-up logging remains in the bridge (`PDAppleSMC` logs every message).

## Work items
| ID | Deliverable | Acceptance on the M1 | Size |
|---|---|---|---|
| M1 | Root-cause defect 1: boot with deep-sleep logging, try the acknowledgement, compare with brcmfmac's handling | Off, 30 s idle, on: no reload needed, 10 times | days |
| M2 | Exercise the two harder off modes (`bwfm_off=1` chip reset, `bwfm_off=2` port shutdown and radio power cut); confirm the SMC "off" value | Lifecycle test passes in each mode | days |
| M3 | Soak (needs SF-B B3): hours of traffic in both directions, repeated join/leave, idle periods; leak and stall counters | 2 hours without stall, leak or fault; throughput distribution reported | days |
| M4 | Multi-CPU correctness (with SF-C C6): audit the single compat lock across interrupt thread, firmware thread, timer, stack and lifecycle paths | Lifecycle and throughput tests pass on 8 CPUs | audit + fixes |
| M5 | WPA3-SAE and protected management frames, following brcmfmac | Joins a WPA3-only network | ~1,000 lines |
| M6 | Regenerate the tree patches including `pd-netd`, SMC, mapper; add the kexts to PureDarwin's arm64 collection list | Boot from the patch-built set passes the join test | 1-2 days |
| M7 | Whole-page DMA exposure: decide between today's in-place mapping and bouncing receive buffers (`PD_BWFM_DMA_RX_SLOTS`), with a throughput comparison | Decision and numbers recorded | 1-2 days |
| M8 | Quiet logging; remove remaining bring-up scaffolding | Normal boot prints one line per stage | small |
| M9 | Roaming, power save, open-network policy (today an interface with no SSID set may join any open network) | Defined behaviour, tested | later |

## Dependencies
SF-B B3 for M3; SF-C for M4; SF-A A8 before Bluetooth shares the port.

## Interfaces this ticket owns
`integrations/puredarwin/m1-bwfm/` and, until SF-A extracts them, the bridge, SMC and DART code in `m1-apcie-host/`.

## Out of scope
Sleep/wake behaviour (SF-K uses the hooks here), Bluetooth (SF-J).
