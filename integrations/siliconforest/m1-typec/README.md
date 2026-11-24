# J293 Type-C port (SF-AF)

First step of the Linux CD321x/ATC/DWC3 port. `SFAppleTypeC.kext` reuses
m1-sound's Linux PA Semi I2C port and SoC power/mapping helpers. It samples
only CD321x **port 1**, one second apart, and publishes raw status and decoded
connection bits in IOKit. It is inert without `sftypec=1`, and requires T8103,
J293 and the expected preserved HPM node/address. The installed loader prunes
port 0 to protect the serial cable.

This provider reports the negotiated Type-C state; PHY/DWC3/ACIO consumers
own their respective hardware startup. Published USB/TB/DP bits alone do not
establish a working host, tunnel or display.

The provider includes Linux's initial S0 check on the preserved
port1. It requires APP firmware, reads SYSTEM_POWER_STATE and sends `SSPS`
with a one-byte S0 payload only when the controller is not already in S0.
It refuses a busy foreign command, bounds completion by one second and an
iteration cap, sleeps between reads, handles rejection/timeout, and requires
an actual S0 readback before publishing a service or cable snapshot. It makes
no periodic command retry. No role swap, mux write,
firmware update or transaction to port0 is added. The command transport uses
the existing serialized I2C engine and Linux's length-prefixed framing.
CIO10 hardware qualification observes initial state7, SSPS completion in two
checks, final S0 and error0. NHI and the desktop remain healthy; the dock
router is still absent, so this is not a complete link fix.

The next source/SDK variant reads EVENT1 and acknowledges exactly that64-bit
word through CLEAR1 before each status sample, following Linux's interrupt
handler order. Zero events cause no write. Failed reads never acknowledge;
failed writes are reported as uncertain and make the sample unavailable.
It uses the existing1Hz timer and workloop; it enables no GPIO/AIC interrupt.
The acknowledgement operation leaves MASK1 untouched. Counters distinguish reads, successful acks and
errors. This does not implement Linux's reset/setup-mask recovery, and it
has not yet been installed or observed clearing events on the M1.

A later startup variant sets Linux's CD3217 MASK1 (plug/power/data events,
bits1/9/10) after APP/S0 validation and before service publication. An already
correct mask needs only a read. Otherwise one exact eight-byte write requires
a checked full readback; any failed or ambiguous setup prevents PHY admission.
It retains slow polled event delivery without enabling GPIO/AIC interrupts.
This is startup setup only: controller-reset recovery and consumer revalidation
are still outstanding. It is not included in the frozen acknowledgement-only
CIO17 image, and has no M1 mask-write evidence yet.

Build: `nix-build build-kext.nix --no-out-link`. The experimental collection
is `build-kc.nix`; it retains the disk/reboot base and adds one kext. Pack
with `SF_KC=<collection> SF_EXTRA_BOOTARGS=sftypec=1
../m1-native-boot/install/build.sh <private-output>`. Never overwrite the
shared install/out; install/reboot follows SF-AF and coordination RULES.

Host test: `host-test/run.sh` checks the actual block decoder's length/error
semantics, little-endian parsing and truncation boundaries under sanitizers.
A second test exercises the actual S0 helper with callback hardware models:
already-S0 sends no command; S3/S4/S5, every I/O failure, foreign busy and
invalid commands, rejection, failed final readback, deadline and stalled-clock
bounds. These host models do not establish CD321x hardware support. Kext lifetime:
timer callback runs on its workloop, samples serialize on the bus lock;
stop cancels/removes the timer before mappings and locks are freed.
The EVENT1/CLEAR1 test exercises all64individual event bits, zero/no-write,
read failure and ambiguous write failure against the actual inline helper.

Kernel clients bind to the `SFAppleTypeC` service and use `copyProperty` on
`port-state` (`sf_cd321x_snapshot`) to obtain a complete status/data/error
sample. Separate display properties may change during a sample and are only
diagnostics. A sample error must not be interpreted as a cable disconnect.
`pd-data-role-host` is the PD role; DWC3's `usb-role` follows DATA_STATUS's
USB connection/role bits. A Thunderbolt-only cable has USB role `none`.

See [SF-AF](../../../docs/roadmap/issues/SF-AF-usbc-thunderbolt-display.md)
for the surveyed chain and hardware evidence. GPL notices: NOTICE and each
source file. Imported code is referenced at build time, never read from
another Linux fork.
