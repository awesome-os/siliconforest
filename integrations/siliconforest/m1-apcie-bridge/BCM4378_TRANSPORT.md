# Original host-only BCM4378 PCIe transport slice

This is a bounded fake-device state machine, not a kext or hardware driver.
Only `BCT_FAKE_BAR_FILE` plus `APB_HOST_TEST_ACCESSOR` compiles the working fake
path. KERNEL plus the fake flag is refused. Default and KERNEL entries return
`BCT_GATED` without I/O; no GPIO152, SMC13, PHY, PMGR, bus-master, reset, firmware
upload, DMA, interrupt enable, allocation or boot path exists. The existing
shared-write escape macro is refused. No packet or collection is prepared.

The existing checked accessor now has an explicitly separate endpoint mode:
`apr_bind_bcm_fake` admits CONFIG (4KiB), BAR0 (32KiB) and BAR2 (8MiB software
cap) IDs9/10/11. They do not alias APCIE root ECAM or PHY. CONFIG writes are
restricted to 0x80 and0x98; word accesses are aligned and end-bounded. All
shared-region writes remain refused. Original `apr_bind_fake` refuses the new
spaces and otherwise behaves unchanged. Exact accessor001 inputs are archived;
the new receipt records current dependency hashes rather than rewriting001.

The steps are separately callable and ordered: PCI identity14e4:4425 read,
4KiB backplane selector at endpoint CONFIG+0x80 with readback and raw BAR0 word,
post-firmware shared-pointer discovery through BAR2, masked mailbox setup,
then at most one opaque synthetic mailbox word and its config doorbell(s).
The PCI identity is grounded in the retained Linux inventory report
`wifi-native-gap-plan.json`; the three reference files use excluded macros for
its numeric value. The raw BAR0 word is not parsed as a BCM4378 chip ID here:
the numeric chip-ID masks/core-base definitions are outside those three files.
No invented identification or complete chip attach occurs.

Only the three pinned ISC brcmfmac files are a protocol facts map. Original
code and policy are rewritten; no Linux implementation, comments or tables
are pasted. In `pcie.c`, the relevant facts are: CONFIG selector0x80 with4KiB
pages (128-130,2199-2207); BAR2 indexes TCM addresses directly (624-647); the
RAM tail yields a changed pointer after firmware startup (2108-2138); shared
flags and mailbox/ring-info pointers are at0/40/44/48 (286-293,1784-1831);
shared versions5..7 and USE_MAILBOX bit25 (223-231); mailbox masks at BAR0+
0x2000+0x4c or0xc34 for core revision>=64 (146-155,563-574,1044-1050,2899-2903);
send data before CONFIG+0x98 kick, twice for core revision<=13 (879-925).
`chip.c:727-728` supplies BCM4378 RAM base0x352000 and limits RAM discovery to
4MiB (112). `firmware.c` requests files asynchronously; this slice neither
requests nor supplies any firmware bytes. Source pin is the acquisition receipt's
6a08a08e57558083f552733a5276d71b1a4d8ff6. No GPL PCIe source is used.

Fake plan RAM size, backplane page, core revision and pre-firmware tail are
explicit inputs, not discovered facts. A zero/unchanged tail returns WAIT;
the fixture's preseeded post-firmware memory is not firmware execution proof.
Shared pointers are aligned, contained in bounded RAM, and nonoverlapping with
the shared prefix/tail/other mailboxes. Only the ring-info prefix range is
validated: no ring is allocated or advertised. Version>=6 without memory
mailbox support refuses. Pending H2D returns BUSY with no write and no poll.
D2H data is preserved. Mailbox interrupts remain masked; the word submitted
is opaque test data, not a claimed operational firmware command.

Every entry and every endpoint read/write rechecks fake PORT_STATUS RUN plus
LINKSTS UP, not BUSY/L2, through the same checked accessor. Link-down at any
step refuses with no new writes. Loss after a payload write retains partial
dirty/fault facts and sends no subsequent doorbell. I/O/verification/partial
transaction failures are sticky; no automatic retries or rollback exist.
Caller owns stable, serialized session/accessor/fake storage. Fake metadata
coherence and accessor context provenance are caller contracts, not an arbitrary
C sandbox. Future EROM/core discovery, firmware lifetime, coherent DMA/rings,
SMC/pin/port ownership, quiescence and kernel transport remain unimplemented.

Run `python3 integrations/puredarwin/m1-apcie-bridge/test_bcm4378_transport.py`.
ASan/UBSan cover fake/default/host-KERNEL modes, all callback fault positions,
all link-down step boundaries and mid-send loss, malformed pointers/versions,
busy mailboxes, exact mask layouts/doorbell counts/order, accessor provenance,
old accessor/observer regressions, negative builds and ten compiled mutants.
Leak detection is disabled for the host environment. No hardware is touched.
