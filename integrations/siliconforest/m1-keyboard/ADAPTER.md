# J293 SPI keyboard adapter

This is an opt-in source candidate for the real J293 built-in keyboard. It is
not a working native keyboard result. Revision 4 was SDK/KC-built. Root's
real M1 enum attempt003 accepted device metadata (count 5, 05ac:0341), then
rejected a CRC-valid interface response whose ID did not echo the request.
The one-probe-per-boot claim held. No descriptor or native input was established.
Root verified Linux recovery; the original failed controller verdict remains
unchanged. Revision 5 below is an unbuilt source correction; further boots
remain on hold. The unchanged board/PIO core passed 19 host tests, metadata
and key-policy checks passed 16, and the actual native wait method passed six
host fake-transport checks, with undefined-behavior sanitization. The earlier
17-test core is preserved in `attempts/core-001/`.
Root separately compiled that earlier core's ARM64 relocatable C objects with
the pinned PureDarwin SDK. Neither check
establishes a linked kext, hardware timing, IOHID enumeration or physical input.

`pdm1kbd=1` is required before the native wrapper maps any registers. The
wrapper also requires the complete J293/T8103 board guard. It does not use a
2021 keyboard tracer, touch the Touch Bar, fingerprint sensor, SPI flash or
display SPI bus. The compositor and existing serial input remain separate.

## Provenance and board qualification

The six reference files under `sources/linux-aurora-keyboard-reference` are
pinned to `6a08a08e57558083f552733a5276d71b1a4d8ff6`. See
`reports/sprint-010/native-keyboard-source-acquisition.json` for their SHA256
and Git blob identities. Their original license notices remain in those files;
license selection for new original adapter code is pending.

The new adapter uses register and transaction facts from:

- `drivers/spi/spi-apple.c:20-196,280-326,378-565`: FIFO registers, controller
  initialization, 8-bit transfers and polled completion.
- `drivers/pinctrl/pinctrl-apple-gpio.c:46-67,174-258`: GPIO/mux fields and
  masked input/output updates.
- `drivers/pmdomain/apple/pmgr-pwrstate.c:20-36,62-135`: power-state fields,
  avoiding replay of BUSY/status bits and the 100-us state-settle bound.
- `drivers/hid/spi-hid/spi-hid-apple-of.c`: keyboard enable 1/0/1 with
  5/5/50-ms waits.
- `drivers/hid/spi-hid/spi-hid-apple-core.c:939-980`: 256-byte packets and the
  separate four-byte write status after a 200-us inter-transfer delay.

The actual saved ADT and original Linux DTB independently identify SPI3 at
`0x23510c000`, main GPIO `0x23c100000`, nub GPIO `0x23d1f0000`, AP mux pins
46–49, enable pin 195 and active-low ready pin 13. The Linux J293 DT specifies
120 MHz reference clock, 8 MHz SPI, mode 0, 65-us CS setup and hold, and 250-us
inactive time. These are board-specific constants, not measured frequencies.
`reports/sprint-010/m1-native-keyboard-path.json` contains source/capture hashes
and the native-driver/console-route findings.

`m1kbd_validate_board` checks guest ADT property lengths and bytes, relative
addresses/ranges, GPIO-function relationships, AIC routing and the PMGR
devices/ps-regs/parent graph. It resolves ADT phandles; Linux phandles are never
used as guest identifiers. All checks precede mapping. The saved real ADT is
an optional private test fixture; the public tests use synthetic properties.

## Hardware effects and refusals

SIO_BUSIF, SIO and always-on NUB_GPIO are read-only prerequisites: actual and
target must both be active, without reset/disable. BUSY is re-sampled for at
most 100 us; a domain remaining busy refuses the operation. The native adapter
does not take ownership from firmware or reset shared SIO parents.
SPI_P, main GPIO and SPI3 have ordinary power domains in the captured Linux
DT. The adapter may request active state for them, in dependency order,
without releasing a reset/disable condition. Already-active registers are
unchanged. Each active-state RMW re-reads the register; the earlier diagnostic
snapshot is never used for a later write. If a domain is auto-gated,
explicitly holding it active clears AUTO
using the reference driver's masked request. Shared clocks stay on afterward;
there is no attempt to roll back or power down shared parents.

No SPI3 register is accessed before its actual power state is active. All six
GPIO words are captured, including LOCK and IRQ group; any LOCK refuses
initialization. Masked writes preserve pull, drive and group fields. Ready is
configured as input with GPIO interrupts disabled; the adapter polls DATA.
It does not guess the ADT `0x06000001` interrupt-group encoding or register a
fake GPIO interrupt controller. SPI local interrupt enables stay zero. The
controller still uses hardware IRQ mode, as in Linux's CPU-polled branch.

Only FIFO PIO is used: no SIO DMA, DART changes, RTKit messages, firmware
upload or new memory-quarantine capability. CS is controlled by SPI PIN bit 1,
not Mesa's hardware-CS delay registers. Inherited nonzero WORD_DELAY refuses
start. Reset's millisecond waits use IOSleep in the native wrapper; FIFO/CS
waits use IODelay and a monotonic clock.

Each read rechecks active-low ready before any SPI transaction. A write holds
CS through 256 TX bytes, 200 us, and four RX status bytes; only
`ac 27 68 d5` is transport success. This is not a response, descriptor or input
event. Root's `native_protocol/dl_spi_hid.c` validates packet/message CRCs and
bounded reassembly. The adapter additionally matches the single pending
semantic request key and metadata identity before any content reaches IOHID.
Request and response IDs are separate diagnostics, not pairing predicates.
Grok's separately owned `protocol/`
engine is for independent comparison and is not selected in this kext target.

A shared 200-ms deadline bounds the entire transaction, including both halves
of a write, with an additional iteration cap if the time callback is broken.
The core rejects FIFO levels over 16 and levels exceeding the destination
remainder **before reading RXDATA**. This avoids the reference driver's
four-byte-status-buffer overflow. Short completion, bad status, MMIO callback
failure or timeout faults the instance; no automatic retry/reset follows.
Receive buffers are invalid on any nonzero result and must not be parsed.

On failure/stop the adapter attempts controller stop, local interrupt disable,
CS hold/deassert/inactive and keyboard enable low. Cleanup failures are retained.
A LOCK refusal performs no SPI register access. Unexpected receive bytes
during a TX-only transfer fail with distinct `M1KBD_RX_UNEXPECTED` (-10);
whether the real controller latches MISO with RXCNT=0 remains unqualified.
The reference ignores these bytes, so this conservative refusal is
diagnosable separately from an overflowing FIFO.
GPIO pinmux and PMGR state are not restored; SPI3 may remain powered. These are
RAM/MMIO state changes only, not boot-storage changes. Bounded software waits
cannot contain a hardware bus transaction that never returns or an EL1 abort;
the external qualified recovery path remains necessary.

## Native ownership contract

`PDM1KeyboardHW` retains physical memory descriptors and uncached maps. Ordered
32-bit accesses use ARM64 barriers. Property objects are retained while the
board guard uses their bytes. Maps are released before descriptors. Its
`closeAfterQuiesce` method requires the caller to disable and join all producers;
the portable core's `busy` flag is not a lock or proof of quiescence.

The native `PDM1SPIHID` uses one workloop and command gate for hardware start
and teardown, and a timer with `kIOTimerEventSourceOptionsPriorityWorkLoop`.
No timer is armed during synchronous boot/metadata enumeration. It fetches
the real device-1 HID descriptor before IOHIDDevice publication, then passes
only validated device-1 input payloads to `handleReport`. The inherited
getReport/setReport implementations return `kIOReturnUnsupported`; no LED or
feature operation is reported successful. Unload and dynamic detach are
refused for this first candidate. Root CMake integration selects the explicit
`pd_m1kbd_stop` antimain, which returns `KERN_FAILURE`.

Shutdown acquires the same gate, marks stopping, cancels the signal callout,
removes the timer, then stops MMIO and releases mappings. The pinned
`IOTimerEventSource.cpp:214-222,272-282,365-397` implements workloop-priority
delivery as a signal-only callout and uses `thread_call_cancel_wait` for it;
the actual driver action runs under the workloop gate. No callback unmaps or
frees itself on a transport failure. Failure to acquire the shutdown gate or
a still-busy transport after joining is an explicit panic, not unsafe release.
Kernel scheduling/lifecycle behavior still requires SDK review and runtime
validation; host fake-MMIO tests do not execute IOKit.

There is no fabricated input report and no assumption that the descriptor is
a USB boot keyboard. Trackpad messages may be consumed and counted separately;
this candidate must not claim native trackpad support.

`inventory/` is root-owned and read-only. It must not open the PDHID queue;
opening `/dev/usb_hid_kbd` grabs input and suppresses text-console routing.
`protocol/` is Grok-owned and `native_protocol/` is root-owned. No files in
these subtrees are changed by this adapter.

## Host verification

```sh
python3 integrations/puredarwin/m1-keyboard/test_adapter.py -v
python3 integrations/puredarwin/m1-keyboard/test_hid_metadata.py -v
python3 integrations/puredarwin/m1-keyboard/test_request_policy.py -v
```

The test compiles only the portable C files into a temporary host shared
library with `-Wall -Wextra -Werror -fsanitize=undefined`. It exercises board
refusal, power sequencing/refusal, masked GPIO writes, locked pins, exact
packet transfers, delays, short/oversized FIFO reports, wrong status, errors
and cleanup. It opens no device. Root owns the SDK/kext/KC builds and every
hardware test. Runtime milestones must separately record binding, real boot
and enumeration, descriptor parsing, genuine key reports, console delivery and
physical typing. Serial virtual HID or a successful fake test proves none of
those native keyboard milestones.

## Revision 3 diagnostics

Revision 2's owned files and receipt are preserved under `attempts/adapter-002/`.
Root reported a successful SDK kext build of that revision; revision 3 requires
its own final build. The board/PIO core, protocol acceptance, descriptors and
lifecycle decisions are unchanged in this follow-up.

Terminal failures record `PDM1KeyboardLastWireHeaderV1`, a 64-byte sequence of
16 native little-endian u32 fields declared by `m1kbd_observation` in
`m1kbd_hid.h`. The same header fields are printed to IOLog: outer flags/device,
fragment offset/remainder/length, validated message command/report/subdevice/id,
response/payload lengths and parser error. The pending request key and outbound
ID are printed separately. Presence fields distinguish missing observations from observed
zeros. CRC-failed outer fields remain explicitly untrusted; the adapter never
guesses a message header from fragment data. No message payload, metadata
string, serial, key bytes or pointer is copied to the diagnostic record. A
hardware read failure reports the last completed packet, not partial new bytes.

Registry counters update only when changed and at most once per 100 ms, with
immediate publication on the first accepted input report and terminal failure.
`PDM1KeyboardCountersUS` gives the monotonic publication time. A counter snapshot
can lag input by 100 ms and is not an atomic snapshot with a userspace TTY read.
The first-report log still does not distinguish an idle/release report from a
physical press. Native report evidence, no host transmit after the launcher,
the human-read nonce marker and physical observation are separate requirements.

## Revision 4 metadata and single probe attempt

The complete a65 revision-3 source, selected protocol and exact receipt are
preserved in `attempts/native-003/`. The recovered Linux log's
`limiting the number of devices to 4` establishes that this controller reports
more than four devices, which revision 3 erroneously rejected. It does not
reveal the exact count or prove that every other metadata predicate passed.

Device metadata retains the 24-byte minimum, 2038-byte maximum and strict
Apple vendor/product 05ac:0341 check. Its minimum count is three, matching
the pinned Linux reference. There is no upper rejection: this driver requests
only keyboard interface 1, with no count-based allocation or enumeration loop.
Interface metadata retains its 16-byte minimum, bounded input-report size and
interface-1 requirement. Optional device/interface strings are unused, so their
offset/length pairs never control acceptance or a memory access. Nothing is
silently substituted for a missing descriptor or actual input report.

Every device/interface validation records its fixed numeric header (24/16 bytes)
as `PDM1KeyboardDeviceMetadataV1` / `PDM1KeyboardInterfaceMetadataV1`, with separate
presence and signed-error properties. Incomplete headers produce a zero record
and presence=0. IOLog prints numeric fields and optional span validity only;
no strings, serial contents, key data or payload tail are read or recorded.
Error values are declared by `m1kbd_metadata_error`: argument -1, length -2,
count -3, vendor -4, product -5, interface -6, input size -7; zero means that
metadata validation passed, not that a keyboard is working. A guard-page host
test makes every byte beyond the fixed header inaccessible while testing
otherwise bounded metadata with invalid optional offsets.

IOResources can rematch a failed, detached driver. Revision 4 therefore uses a
module-global, aligned UInt32 claim with OSCompareAndSwap in `probe`, after
`pdm1kbd=1` is checked and before calling the superclass probe. Exactly one
opt-in probe can claim the attempt during the module's lifetime. The claim
is never cleared, even after allocation, board, start or metadata failure;
the existing unload refusal keeps it until reboot. Further probes return null
without mapping or MMIO. Numeric registry IDs distinguish the winning claim
from refused matches. This is an attempt limit, not callback synchronization;
the existing per-instance workloop/gate teardown remains unchanged. Its host
test checks the native source policy only; SDK and runtime behavior remain
separate evidence.

No protocol CRC, framing, board/PIO, descriptor bound, quarantine or teardown
rule changes in revision 4. Root owns SDK004 and subsequent review. **No new
hardware boot is authorized by this source freeze; the user's hold remains.**

## Revision 5 semantic pairing and one-use keys

The full df1b revision-4 source set, selected protocol and receipt are archived
unchanged in `attempts/native-004/`. Root reports enum attempt003 observed a
CRC-valid interface response with flags 40, device d0, command 20, report 02,
subdevice 01, response ID 0, response length 512 and payload length 31; the
outbound ID was 1. No raw payload is included here. The pinned Linux reference
`spi-hid-apple-core.c:149-150` describes a separate ID counter per message type,
while line 309 assigns outbound IDs from one global counter. Its response
dispatcher at lines 721-736 selects command/report/subdevice, with no echoed-ID
comparison. The observation establishes that echo equality is invalid; it does
not fully qualify all firmware counter behavior.

Revision 5 removes ID equality and the ID argument from the matching helper.
Both CRCs, framing/reassembly bounds, flags 40, device d0, command 20 and exact
pending report/subdevice checks remain. Response length remains diagnostic;
actual payload bounds and descriptor/metadata validation are unchanged. The
first non-input response with the wrong semantic identity is still fatal;
there is no fallback, retry or search for a later matching response. The
one-second deadline and 1000-poll bound are unchanged.

`usedRequestKeys` starts at zero once in instance initialization. Immediately
before incrementing the outbound ID, encoding or sending, `m1kbd_claim_request`
permits exactly these command-20 keys, each once: 01/d0 (device info), 02/01
(keyboard interface info), and 10/01 (keyboard descriptor). The bits are 1, 2
and 4 respectively. Unknown keys, repeats and invalid masks fail without
modifying the mask or sending a request. Claims are never released, including
after encode, transport, CRC or response failure. Enumeration is synchronous
under the existing gate, with no timer running; the global atomic probe latch
prevents a new instance from repeating the sequence. This supports semantic
association for this bounded, single-outstanding enumeration only; it is not a
general request protocol for retries, parallel requests or resume.

Host tests exhaust unknown report/subdevice keys, exercise all six valid key
orders and repeated claims, and reject every wrong semantic field. The
`test_request_policy.py` harness compiles the actual `waitResponse` method body
with the real C encoder/parser/claim helpers and fake transport/time/teardown.
It checks differing response IDs, no writes or ID increments on invalid/repeated
keys, permanently consumed claims on failure, first-wrong-response termination,
background-input handling and both timeout limits. Source-policy checks tie the
mask initialization and sole SPI request-write site to that method. These tests
do not execute IOKit, MMIO, callback teardown or a native keyboard.

Board/PIO, the root-owned protocol engine, metadata/descriptor bounds, global
probe latch and lifecycle code are unchanged. Root owns SDK005/KC construction
and any later authorized test. **Revision 5 source readiness does not authorize
another boot or establish descriptor publication, input or physical typing.**
