# Pinned identity reference facts

`header-manifest-001.json` adds three exact-commit ISC header references to the
existing three-file ISC protocol map. The explicit acquisition command is:

```sh
python3 tools/acquire_sources.py fetch \
  --manifest integrations/puredarwin/m1-apcie-bridge/identity-reference/header-manifest-001.json \
  --cache .source-cache --allow-network
```

This command verifies the expected Git blob before accepting each file. It does
not update a branch, registry pin or existing source freeze. Header bytes remain
in the ignored source cache. They are references for factual definitions, not
implementation files to include or copy into a Darwin driver.

`register-facts-001.json` records the full byte hashes, Git blobs, source lines,
integer values and scope. A separate independent review is recorded in
`reports/sprint-010/bcm4378-identity-header-gap-001.json`.

The verified facts close the earlier missing numeric-header references: the
enumeration base, CHIPID offset and masks/shifts, AI type, BCM4378 chip ID and
PCI device ID. The chip ID is hexadecimal `0x4378` (decimal17272). These source
facts agree with the existing synthetic profile; they do not establish a real
read from the chip. The frozen fake model and its `numeric_profile_verified=0`
remain unchanged.

Linux sysfs PCI revision is a different value from ChipCommon chip revision.
The firmware masks in the pinned `pcie.c` select on ChipCommon revision. The
presence of B1/B3 filenames in a firmware directory does not prove which image
was requested, loaded, valid or calibrated. No firmware, NVRAM, CLM, TX-cap or
signature contents were acquired or staged by this work.

Real endpoint reachability, core/EROM/RAM discovery, prepared backing, DART
ownership, MSI handling, firmware initialization and safe teardown remain
separate requirements. Shared APCIE PHY/PMGR writes remain compiled out. The
prior readonly observation and hardware STOP are unchanged; these references
authorize no boot, initialization, mode change or physical device access.
