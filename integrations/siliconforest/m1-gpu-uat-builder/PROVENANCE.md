# Provenance

All new implementation, tests and documentation here is original. Original-code
license selection remains pending. No upstream implementation was copied.

This leaf composes these frozen original sources without modification:

- `m1-gpu-uat-codec`, source set
  `b174c6022d2d5bf5425dabe77c4b229578ae0fa5c0cb9ea47ee980ed40dde835`.
- `m1-gpu-uat-private`, source set
  `a231dced0d0615647e956698c84b8140e87e0db3b8e1c405a0f07bcb074c5682`.

The codec's numeric table format comes from pinned MIT Omacom/m1n1
`121fcbf35d4501814b58ca5347926a4173b6a512`. The allocation contract and relevant
KPI exports come from PureDarwin/XNU pin
`e7aa00a60f0b56ef35aa24252e81467787bf71ea`, recorded in
`reports/sprint-010/gpu-uat-adapter-design-001.json`. `MapperNone`,16KiB sizing,
36-bit admission and explicit prepare/complete pairing remain the unchanged
private allocator's behavior. The36-bit ceiling is conservative policy, not the
T8103 UAT hardware output width.

`gpu-t8103-firmware-abi-evidence-001.json` selects V13_5/G13 from existing boot
metadata but explicitly leaves loaded GPU image ABI and ownership unproven.
This builder does not consume firmware metadata, create firmware objects, select
initdata layouts or import preexisting tables. Its detached table-only graph is
independent of those unresolved firmware contracts.

The new mutable bridge is a separate, explicitly scoped extension: it reads the
exact frozen owner's internal records under exclusive serialization, verifies a
fresh provider view, and obtains writable bytes from that same owned IOBMD.
It does not add mutation to the frozen provider API. The builder writes only
private table pages; snapshots remain ordinary immutable CPU data while the
frozen validator runs. This composition is pinned to predecessor hashes, not a
stable or general kernel ABI.

Compilation uses the existing immutable OSS SDK6zpwyx6, ARM64 kernel headers
rxi8m8, and cross toolchainv0x4y2. Exact full paths, compile flags and transitive
dependency hashes appear in `validation.json`. The existing pinned
`pd_apple_builtins.h` compatibility shim is used, as in the predecessor SDK build;
its allocator-hardening limitations remain unchanged. No SDK derivation, KC,
service or target execution is created here. Five relocatable objects are
compiled and hashed, with unresolved external symbols checked against the
selected IOKit/Libkern/Mach export lists. Virtual calls do not become direct
imports and still need eventual runtime ABI evidence.

The tests reverify the frozen codec, private allocator and RTKit control receipts
before building. Tests allocate synthetic host memory; manually disposing of
retained fake pages at test-process teardown is not a production reclamation
path. Nothing in this leaf authorizes hardware access or changes GPU observer HOLD.
