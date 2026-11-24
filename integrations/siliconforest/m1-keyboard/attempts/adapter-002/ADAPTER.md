# J293 SPI keyboard adapter

This is an opt-in source candidate for the real J293 built-in keyboard. It is
not a native keyboard runtime result. The revised board/PIO core passed 19
host tests and the metadata guards passed 5, with undefined-behavior
sanitization. The earlier 17-test core is preserved in `attempts/core-001/`.
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
bounded reassembly. The adapter additionally matches request IDs and metadata
identity before any content reaches IOHID. Grok's separately owned `protocol/`
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
