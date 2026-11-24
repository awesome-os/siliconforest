# Provenance

Implementation, tests and documentation are original; original-code license
selection remains pending. No GPL implementation is copied. The test runner
extends the project's original frozen builder runner in this new leaf only.

Unmodified frozen dependencies are `m1-gpu-uat-codec` (source set b174c602),
`m1-gpu-uat-private` (a231dced) and `m1-gpu-uat-builder` (6f724297). Their exact
full hashes and the unchanged RTKit control receipt are reverified by the test
runner. Composition is pinned to their current internal owner/view records,
not a general or stable kernel ABI. DATA references are independently created;
the table-only private provider is not reinterpreted as a data provider.

Numeric descriptor facts come from MIT Omacom/m1n1 pin
`121fcbf35d4501814b58ca5347926a4173b6a512`, `proxyclient/m1n1/hw/uat.py`, SHA256
`4a4d0300cd3a4ed4fb3de250866993b0d1e00ec3249f93077a871e69c94f0838`.
Lines24-28 define Shared=2 (the upstream comment itself says “Probably”);
lines60-72 define OS55, UXN54, PXN53, nG11, AF10, SH9:8, AP7:6, AttrIndex4:2.
Lines107-116 interpret OS1/AP1/UXN1/PXN1 as GPU read-only and
OS1/AP0/UXN1/PXN1 as GPU read/write. This leaf fixes SH0/AF1/nG1, sets both XN
bits, and allows only those two semantic cases: flags 0x00e0000000000c48 and
0x00e0000000000c08. The codec adds TYPE/VALID and checked physical address.
These are source-interpreted detached descriptor policies, not proof of actual
T8103 access permissions, cache attributes, execute prevention or coherence.

IOBMD allocation/prepare/complete source and KPI facts remain pinned to XNU
`e7aa00a60f0b56ef35aa24252e81467787bf71ea` and the exact source files in
`gpu-uat-adapter-design-001.json`. The 36-bit PA ceiling is conservative policy,
not a new hardware-width claim. Factory wiring/auto-prepare remains distinct
from this provider's one explicit successful prepare/complete pair.

`gpu-t8103-firmware-abi-evidence-001.json` selects the V13_5/G13 reference
profile from already captured boot metadata. Loaded GPU image ABI remains
unverified. No firmware metadata or image enters this implementation.

`validation.json` records actual immutable OSS SDK, ARM64 headers, toolchain,
compile flags, transitive dependency hashes and seven object hashes. No
derivation, linked KC, kext or target executable is built. The inherited
pd_apple_builtins.h compatibility shim is retained. Export coverage proves only
direct symbol presence in the pinned source lists: C++ virtual calls and runtime
semantics remain unproven. Synthetic teardown frees retained fake storage only
to let host sanitizers check the test process, not as a production reclaim API.
