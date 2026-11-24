# Provenance and limits

All implementation, AST reader, tests and documentation here are original.
Original-code license selection remains pending. No GPL source was copied.

Primary numerical reference is the already acquired MIT Omacom/m1n1 commit
`121fcbf35d4501814b58ca5347926a4173b6a512`; copyright The Asahi Linux Contributors.
`source_facts.py` binds six local files by full SHA256 and verifies each against
the exact Git commit object. `validation.json` records all hashes, source lines,
derived sequential field offsets and golden byte strings. It reads syntax as
data with an explicitly restricted parser; it does not execute source modules
or represent a Construct runtime roundtrip.

Relevant reference facts are:

| File under proxyclient/m1n1 | Evidence |
| --- | --- |
| fw/agx/channels.py:87,146,160 | V>=V13_3 DC_Init0x1a, UpdateIdleTS0x23, 44-byte zero defaults; only G14/V>=V13_2 expands DeviceControl to64 bytes, so G13 remains48 |
| fw/agx/channels.py:445 | FWCtl field order: u64/u32/u32/u16/u16, total20, offsets0/8/12/16/18 |
| fw/agx/channels.py:469,487,493 | Ring counts256 and strides48/20; normal state48/write32 versus FWCtl state32/write16; both read0 |
| agx/channels.py:34,75,130 | Source modulo advance; default DC patterns; FWCtl sender assigns zero/one/two constants and address default0 |
| agx/__init__.py:138,183,191,365 | State allocation uses48 bytes; control/FWCtl allocations; source start sends initdata before DC_Init and UpdateIdleTS |
| fw/agx/initdata.py:10,1626 | FWCtl ChannelInfo resides in FWStatus, while ordinary ChannelInfoSet belongs to RegionB; no initdata layout is encoded here |
| constructutils.py:793 | Ordered version/generation matrix; explicit V13_5/G13 AST selection |
| hw/uat.py:242,255,511 | Pre-G15 root bit39, 16KiB pages and upper-half sign extension, already covered by the frozen UAT codec |

The address-range, nonalias, strict zero-default, bounded subset and
one-empty-slot fullness rules are original conservative validation policies.
They must not be attributed to the firmware as established protocol semantics.
Context IDs remain opaque; no context bound was invented from an unproven table.
No source instruction is translated into a hardware action.

The existing `gpu-t8103-firmware-abi-evidence-001.json` selects V13_5/G13 as a
reference profile from historical captured boot metadata; it does not identify
the loaded GFX image ABI. This codec therefore refuses every requested loaded
or metadata-derived profile. The explicit SOURCE-only profile cannot prove any
current firmware message format is safe to use.

Frozen codec/private/builder/data/RTKit receipts are reverified byte-for-byte.
They are preservation checks, not runtime dependencies of this standalone C
object. The SDK and cross-toolchain paths, compiler flags, transitive headers
and object hash are in `validation.json`. Compilation verifies an ARM64 Mach-O
object with zero undefined symbols and a512-byte frame-warning limit. There is
no kernel adapter, IPC, firmware execution, source installation or GPU result.
