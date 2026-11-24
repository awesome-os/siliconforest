# Apple DWC3 / xHCI port (SF-AF milestone 1, in progress)

`build-xhci.nix` builds an experimental replacement RavynXHCIPort bundle
against the SiliconForest ARM64 SDK. USB03 is installed as an experimental
collection; the default collection recipe remains unchanged.
`build-dma.nix` builds the DMA adapter alone. Neither has performed controller
DMA on the M1; the installed port-1 sampler belongs to `../m1-typec`.

`patches/apple-dma-lifetime.patch` retains upstream's MIT notices and binds a
prepared `pd,apple-dwc3-host` IOPlatformDevice. Its parent must initialize the
ATC PHY, DWC3 core and paired DART before publication; its
`SFAppleUSBDart` platform function supplies the kernel-lifetime DMA domain.
The xHCI driver validates DWC_usb31 identity and host role before attaching.
The same bundle keeps the existing PCI/A733/Tegra paths.

Every xHCI allocation and buffer address (DCBAA, scratchpad, context, command,
event, EP0, HID, bulk, isochronous and storage bounce buffers) uses the domain
on Apple. CPU physical addresses remain distinct from device addresses.
MSC direct client mapping is rejected by the existing noncoherent path, so
payloads bounce. Both physical and IOVA alignment honor 64KiB TRB boundaries.
`tools/mk_xhci_patch.py` regenerates the patch from the reference SDK tree and
asserts each expected replacement; it does not modify that checkout.

The USB1 domain shares one table root and allocator between ADT `dart-usb1`
reg 0 / SID 0 and reg 1 / SID 1, matching Linux `t8103.dtsi`. It references
the existing DART table implementation at build time. TLB failure poisons
the domain and quarantines backing; tables last for the kernel. `prepare()`
requires the controller held in reset and no concurrent DMA producer.

Removal closes admission, disables interrupts, wakes and drains synchronous
calls and retained hotplug/bulk workers, then requires successful xHCI halt
and reset. `SFAppleUSBDrain` returns busy if that fails; the parent must keep
the PHY, controller and DMA alive then. `sf-usb-dma-live` on the provider also
covers failed attachment. Successful stop aborts queued transfers, terminates
children and breaks bus/storage ownership cycles. Old clients retain their
controller and receive offline; descriptors unmap before releasing pages.

`host-test/run.sh` exercises production admission under concurrent removal
(100 cycles, eight callers) with ASan/UBSan, and compares the production IOVA
search against 4,194,304 independent fragmentation/alignment expectations.
`make -C ../m1-dcp-dart test` covers the referenced page tables. These tests
and SDK compilation do not establish MMIO, cache coherency or hotplug on M1.

Hardware validation still requires direct USB enumeration/HID/read-only
storage/DHCP, then ten hotplug cycles. SuperSpeed lane calibration follows
USB2. See SF-AF for the
verified baseline and shared-M1 install/recovery rules.

`build-host.nix` adds the actual port-1 `SFAppleUSB` lifecycle personality to
this bundle; `build-kc.nix` replaces the original xHCI bundle in an isolated
collection and includes the Type-C snapshot provider. Boot arguments are
`sftypec=1 sfusbhost=1`. The daily collection is unchanged. Source and ARM64
collection build are complete; this host collection is not installed yet.

The lifecycle consumes copied `port-state` snapshots. Failed I2C samples
preserve the session. It powers/configures only a direct host cable with no
DP/TBT/USB4 flag; the TS3-only state remains deferred. It resolves and checks
all port-1 windows, validates tunables before writes, establishes stopped
controller ownership, parks/reset/powers the PHY, prepares DART, initializes
DWC3, selects USB2 host after core reset and publishes the xHCI nub. Removal
requires the child's drain and synchronous termination before PHY shutdown.
A failed attachment that still owns DMA also blocks shutdown and retry.
DART faults stop further sessions. Domains/tables live for the kernel.

The DWC3 host-only port checks the DWC_usb31 identity and capability, clears
PHY suspend during initialization, performs bounded device-half soft reset
when needed, applies the global-control/2.00a clock-gating workaround and
Apple CIO settings, disables gadget event DMA, then sets host role/suspend.
Clock timing defaults are retained as in the T8103 Linux DT, which supplies
no reference/suspend clock overrides. Host tests cover all role/power-option
combinations, reset ordering, invalid identities and timeout refusal.

Remaining hardware work starts with a captured first boot and direct USB2
attachment, followed by enumeration, HID, read-only storage, DHCP and ten
hotplug cycles. Common tunable/MMIO correctness, DART DMA/cache behavior and
IOKit removal are not proved by register models or SDK compilation.

The newer native adapter selects USB3 when the immutable Type-C sample
reports USB3 and still defers every DP/TBT/USB4 cable. It copies and retains
the runtime lane bundle before PMGR/PHY writes, powers/calibrates the
correct orientation, initializes DWC3, then routes PIPE before publishing
xHCI. The qualified T8103 path skips host BIST, matching Linux's workaround.
A change in USB2/USB3 mode drains the old session. Stop parks PIPE only
after native controller drain and synchronous termination; failed handoff
completion keeps PHY ownership and blocks a new session. No calibration
values are shipped. This source SDK-builds but is not installed in USB03.


The owner has selected the permanently attached TS3 Plus as the first target.
Direct-host-only boots are deferred. USB04 is installed, but no direct or dock
USB device has enumerated in Darwin. The following shared PHY change is source
and SDK evidence only; no ACIO/NHI client has yet acquired a native lease.

`SFAppleUSB` now provides a kernel-only `SFAppleUSBCPhy` interface for exclusive
TBT, USB4 and four-lane DP sessions. Cable epoch and non-reused token identify
the owner; a changed cable revokes admission without resetting another client's
PHY. A revoke notification runs outside the provider's command gate and only
asks the client to schedule teardown. Release requires a nonblocking, factual
client drain check after controller/IRQ/DMA/display users have been joined.
Failed drains retain the owner; failed PHY shutdown quarantines the provider.
The command gate serializes timer and client transitions. Tokens alone prove
no hardware fact. Composite USB3+DP remains unsupported by this exclusive API.

Initial handoff checks the real stopped DWC3 view and ACIO1's three physical
power gates before enabling PHY clocks. T8103 M3 is covered by ATC1_CIO;
its clock-gate entry is a virtual reconfiguration command without a power
state register. The check uses power-gates, not that virtual clock vote. The read-only private PMGR extension distinguishes
unknown state from off, checks all ADT gate/address bounds, and rejects active,
clock-gated and automatic-wakeup domains. It is appended only to this bundle's
copy of sf_soc.cpp, leaving the sound owner's helper untouched. The state property aggregates raw physical gate bits; its error property must
be zero to establish off. Physical addresses are checked against the qualified
T8103 domain table before reading. No fabricated M3 power register is used. A preexisting live ACIO must be stopped by its actual
owner, not bypassed using this API. This off check also protects direct-host
initialization. A poisoned prior USB DART prevents alternate-mode startup.

TBT/USB4 mode setup uses the already ported runtime ATC calibration. DWC3 remains
unpublished; only USB4 routes PIPE to the USB4 backend. DP AUX is enabled after
successful mode setup; partial failure cleanup touches AUX only if this session
began enabling it. Rate changes use the actual DP/tunnel register backend;
DCP's zero DP-rate notification remains the upstream no-op, not a clock-off ACK.

Host ASan/UBSan checks add 10,000 revoke/retry/stale-token cycles, failed-drain
retention and counter-overflow quarantine, plus the actual PMGR extension with
malformed/unknown/active/auto/mapping-failure fixtures. Existing DMA/lifetime,
all-mode ATC and DP clock/timeout suites pass. Neither these tests nor the SDK
build establishes M1 gate state, TBT link, NHI DMA or dock PCI enumeration.


The next diagnostic candidate snapshots the retained ATC tuning register
addresses, with a maximum512 entries. It validates every compact group and
the entire address list before reading any calibration register. Records are
three little-endian32 words: window0/core or1/AXI2AF, offset, actual value.
It reads no fuse-controller register, firmware storage or arbitrary address.
There is one capture after exclusive TBT/USB4 PHY configuration while prior
ACIO is proved off and before committing the client lease; later captures
require the live owner/token through the existing bounded snapshot command.
The pre-ACIO and later properties have independent error/count fields. No
mode/rate write or timer is added. Values are live device calibration evidence
and must remain in ignored private captures. Native SDK/collection builds and
host ATC suites pass; this readback extension is not installed yet.


CIO12B qualifies the calibration readback on the M1:250 records/174 unique
addresses, matching the active Linux address set; all programmed masks match
expected values. NHI remains healthy but no dock router is discovered.

The next TBT-only startup step enables the documented dummy-PHY bit inherited
from m1n1's Linux boot baseline. Fresh ACIO-off and actual stopped/reset/
clamped DWC3/park readback precede it; every other PIPE/AON field is preserved.
No direct USB host starts. The helper and native collection build/test pass;
this initialization is not yet installed or hardware-qualified.
