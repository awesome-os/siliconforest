# PDApcieT8103Bridge: read-only apcie,t8103 port-0 bridge skeleton

Step W1(a) of `reports/sprint-010/wifi-darwin-netif-gap-001.json`: the first
source for the PCIe layer under the J293's BCM4378 Wi-Fi. It **observes**
port 0 and changes nothing. It is not yet an `IOPCIBridge`: it publishes no
nubs, does no config-space access, link training, PERST, clock, power, PHY or
DART action, and owns no device. Not linked into any kernel collection; no boot
is staged.

## Match

`IOResources` + boot argument `pdapciebridge=1` (absent or any other value does
nothing), one probe per boot (atomic claim never cleared), unload refused. The
same pattern as `PDM1SPIHID` and `PDDARTSurvey`, which also read the device tree
by path on this kernel.

Then two ADT-only checks must pass before any register access:

1. The frozen m1-dart-survey revision-2 `board.c` (`dls_validate_board`, used
   unchanged): J293 / MacBookPro17,1 / `arm-io,t8103`, `apcie,t8103` with
   `#ports=3`, clock/power gates `[77,97,29]`, the exact RC, PHY and port0
   `reg` tuples, and the PMGR domain table that yields the four guard words.
2. `apo_validate_bridge`: `/arm-io/apcie/pci-bridge0` is `apcie-bridge` with a
   `function-perst` property, its `wlan` child is exactly
   `wlan-pcie,bcm4378\0wlan-pcie,bcm\0`, and `pci-bridge1`/`pci-bridge2` are absent.

## Staged reads (aligned with the m1-dart-survey guard)

Read-only, lazy `kIOMapReadOnly | kIOMapInhibitCache` maps of 16 KiB prefixes
of PMGR, RC, PHY and port0 only (DART is never mapped), `kIODirectionIn`
descriptors, one `volatile const uint32_t` load between `dmb osh` barriers.
Exactly ten whitelisted (region, offset) pairs exist; at most 14 reads.

| Stage | Reads | Continue only if |
|---|---|---|
| Power | PMGR IMX `+0x1b8`, APCIE `+0x348`, APCIE_GP `+0x3e8`, PCIE_REF `+0x1a0` | each is on: low byte `0xff`, no `0x90001c00` bits (survey predicate); stop at the first that is not |
| PHY | PHY CTRL `+0x000` | `(v & 0x8f) == 0x0f` (CLK0/1 REQ+ACK, T8103 reset bit7 clear) |
| PHYIF | RC PHYIF CTRL `+0x024` | RUN bit0 |
| Port 0 | APPCLK `+0x800`, RESET/PERST `+0x814`, STATUS `+0x804` (all three recorded) | STATUS RUN bit0 |
| Link | LINKSTS `+0x208` (UP bit0, BUSY bit2, L2 bit6) | — |
| Final | the four PMGR words again, after any read past Power | otherwise result POWER_LOST |

The only difference from the survey guard: once PHYIF RUN allows the port block,
all three port control words are recorded instead of stopping at the first clear
bit, because this observer gates nothing behind them. LINKSTS is read only after
STATUS RUN, matching m1n1's own order.

Register offsets come from m1n1 `src/pcie.c` (MIT) as facts; see `NOTICE`. No
GPL Linux or Apple code is used.

## Output

`PDApcieBridgeObservationV1`, an 84-byte little-endian snapshot
(`observe_layout.h`), set on the IOResources provider and on the instance, plus
one IOLog line with the raw words. `valid_mask` says which fields were read.
Result codes: 0 OBSERVED, 1 POWER_NOT_READY, 2 PHY_NOT_READY,
3 PHYIF_NOT_RUNNING, 4 PORT_NOT_RUNNING, 5 POWER_LOST, -1..-4 argument, board,
IO, limit.

With nothing initializing PCIe today, the expected first result is
PHY_NOT_READY, the state the DART survey's guard already recorded. That is an
observation, not a failure of the skeleton.

## Checks

`python3 test_observe.py [--adt <saved guest ADT>]`: C tests under ASan/UBSan,
static no-write checks, and (with `--adt`) both ADT checks against a saved J293
device tree. `build-bridge-observe.nix`: SDK compile of the standalone bundle
only. Results are in `source-validation.json` and `validation/`.
