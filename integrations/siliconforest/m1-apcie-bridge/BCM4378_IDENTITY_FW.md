# Original fake chip-identity and firmware-request slice

This slice rewrites facts from the pinned ISC `chip.c` and `firmware.c` map;
firmware family/item facts additionally use the previously frozen three-file
note and transport evidence. No implementation, comments or tables are copied.
No GPL file is a copy source. No filesystem loader, firmware bytes, radio start,
SMC/GPIO or hardware backend is provided. The lead's read-only packet and all
existing source freezes remain unchanged.

The fake register path names and checks main facts explicitly: APCIE port RUN
at0x804, LINKSTS UP/non-BUSY/non-L2 at0x208, endpoint PCI identity14e4:4425 at
CONFIG0, current4KiB ChipCommon selector at CONFIG0x80, and ChipCommon CHIPID
at BAR0+0. The selector must already equal the supplied profile page; identity
does not write it. All I/O goes through the explicit BCM mode of the checked
accessor. Fake write callbacks are traps; identity and every request write zero
device registers. Firmware requests also recheck PCI identity before callbacks.

**Numeric source gap is explicit.** `chip.c:947-974` references
SI_ENUM_BASE_DEFAULT and CID_ID_MASK/CID_REV_MASK/SHIFT/CID_TYPE_MASK/SHIFT
defined in excluded headers. The pinned map does not numerically establish
those definitions. Thus there is no implicit hardware profile. Callers supply
a synthetic fake profile with checked contiguous/nonoverlapping32-bit fields;
AI type1 is explicit in chip.c. The tests use a labelled synthetic page
0x18000000, ID mask0xffff/expected0x4378, revision bits19:16 and type31:28.
These fixture numbers are **not claimed independently verified hardware facts**.
`numeric_profile_verified` always0, even when the fixture is accepted. A later
hardware driver needs exact independently sourced profile constants and actual
chip/revision/EROM evidence. A passing fake does not close that gap.

The pure plan builder requires an accepted fake identity, then selects the
already mapped family: revisions0..3 use brcmfmac4378b1-pcie;5..31 use
brcmfmac4378b3-pcie;4 and>=32 refuse. The five requested items are executable
`.bin` (required), board NVRAM `.txt`, regulatory `.clm_blob`, transmit capability
`.txcap_blob`, and signature `.sig` (the latter four optional in the reference
request API). They are item requests, not proof all are consumed before CPU
start: CLM/TXCAP are retained for later driver use. Optional absence does not
certify J293 calibration/regulatory/radio correctness. No start API exists.

`firmware.c:621-676,803-864` establishes inserting caller-provided qualifiers
before an extension, ordered alternatives then canonical fallback, and the
32-bit revision guard. Qualifiers here are explicit inputs (up to6x63bytes),
not guessed J293/OTP/antenna identities. Duplicate/invalid/path-like qualifiers
refuse; names are bounded leaf strings only, not a directory or a claimed
expansion of the excluded BRCMF_FW_DEFAULT_PATH macro. No NVRAM parsing, EFI
access, filename scan, firmware upload, signature verification or WCC seed
construction is implemented. Those remain separate genuine work.

Each fake request accepts present/absent/error status from an in-memory manifest
callback only, with no data payload. Fresh link/PCI checks precede every variant;
a down link returns LINK_DOWN with zero request calls and zero device writes.
A loss between variants preserves the completed prefix and refuses the next.
Mandatory absence is MISSING; optional absence is OPTIONAL_MISSING, not a found
item. Fake presence is no firmware-byte, checksum, compatibility or boot proof.

Only BCI_FAKE_DEVICE with APB_HOST_TEST_ACCESSOR enables the fake device path.
KERNEL+fake and the shared PHY/PMGR fake-write macro are compile errors.
Default/KERNEL identity/request functions return GATED with no I/O/callback.
The pure metadata/name functions never perform MMIO in any mode. Caller owns
stable, serialized buffers/context; no callback may reenter or open a device or
download firmware. A C interface is not an arbitrary-code sandbox.

Run `python3 integrations/puredarwin/m1-apcie-bridge/test_bcm4378_identity_fw.py`.
ASan/UBSan cover fake/default/host-KERNEL paths, chip-field/profile/error checks,
all33 tested revision inputs0..32, exact item extensions and qualifier order,
all five items refusing under four down-link states, partial request/error
evidence, output preservation and buffer caps. Twelve compiled assertion-killed
mutants and negative builds are included. Host leak detection is disabled.
This is source/host evidence only, not a kext, firmware request against a real
filesystem, hardware identification or boot packet.
