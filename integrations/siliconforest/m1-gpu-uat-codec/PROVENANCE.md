# Numeric provenance and unresolved runtime contract

Primary reference: already acquired `sources/m1n1-baseline`, origin
`https://github.com/omacom/m1n1.git`, commit
`121fcbf35d4501814b58ca5347926a4173b6a512`. Its root LICENSE is MIT, copyright
The Asahi Linux Contributors. No reference Python module is imported or
executed by this leaf. New code is original; its license selection remains
pending under the repository policy. No GPL implementation was copied.

The inspected `proxyclient/m1n1/hw/uat.py` has SHA256
`4a4d0300cd3a4ed4fb3de250866993b0d1e00ec3249f93077a871e69c94f0838`.
The receipt checks it byte-for-byte against the pinned Git object and binds the
other consulted reference files. Facts used from that file are:

| Reference construct | Numeric fact used |
| --- | --- |
| `TTBR` | VALID bit0; BADDR bits47:1; ASID bits63:48 |
| `PTE` | VALID bit0; TYPE bit1; AttrIndex bits4:2; AP bits7:6; SH bits9:8; AF bit10; nG bit11; address bits47:14; PXN/UXN/OS bits53/54/55 |
| `Page_PTE.valid` | Page validity requires both low bits set |
| `UAT_L0_OFF` and `UAT.LEVELS` | Before G15: root selector39, then indices36/25/14; sizes2/8/2048/2048 |
| `PAGE_BITS`, `iomap_at` | 14-bit page offset and aligned 16 KiB table allocations |
| `get_pt`, `flush_pt` | Little-endian unsigned 64-bit descriptor words |
| `recurse_level.extend` | Upper root is presented with bit39 sign extension |

The already acquired hardware descriptor
`sources/linux-aurora-gpu-reference/drivers/gpu/drm/asahi/hw/t8103.rs`, at the
existing recorded pin `6a08a08e57558083f552733a5276d71b1a4d8ff6`, identifies the
T8103 GPU generation as G13. Only that numeric classification is used. Its
SHA256 `e789d336377c8f07ae1d3473b419061d858aa0a53f2f2f9b6396433d7a76a9c6`
matches the prior GPU preflight reference receipt; this is a hash check of an
already acquired partial reference, not a new full-checkout verification.
The MIT `constructutils.py` maps the platform's SoC generation to the AGX G
generation. No host environment variable or runtime auto-detection selects the
profile here: this leaf is explicitly the restricted T8103/pre-G15 profile.

Admission rules deliberately narrower than the reference are policy, not new
hardware claims: only a zero raw word is a hole; intermediate descriptors must
be plain TYPE1 pointers with no page flags; blocks and undeclared bits refuse;
all table addresses must be 16 KiB aligned; canonical input is mandatory;
table sharing is forbidden; all supplied nodes must be reachable; whole pages
must lie in caller ranges; and table/data ranges must be disjoint. The reference
has a block predicate, nonzero invalid entries, firmware-shared table reuse,
and masking of VA inputs. This leaf cannot claim to accept every live firmware
table or to walk arbitrary captured hardware state.

Page flag fields are retained as raw numeric fields, including undocumented
attribute indices. Neither ARM permission names nor the reference's AGX/FW
access display tables are used to invent a safe permission policy. In
particular, AF, OS, AP, PXN, UXN and AttrIndex values do not authorize a device
operation merely because the bit layout roundtrips.

The following are exact missing contracts before any future runtime use:

1. Actual J293/T8103 power/clock/reset readiness and a serialized owner for the
   GFX/ASC domains, with a reviewed recovery path. The held preflight observer
   supplies no runtime result; the build's resolved imports supply none either.
2. A physical allocator/IOMMU ownership contract that binds stable, correctly
   aligned RAM pages to exact allocation identities, excludes firmware and
   protected carveouts, and establishes CPU and device access rights. Numeric
   caller ranges and a successful graph validation do not establish this.
3. Firmware-version and context lifecycle evidence for actual root location,
   TTBR/ASID installation, handoff synchronization, shared FW mappings and
   invalidation ordering. The reference calls handoff and maintenance routines;
   that does not make copying their sequence a safe Darwin backend.
4. Proven cache/coherency and TLB rules for publication, replacement and
   teardown, plus in-flight GPU/firmware quiescence and fault behavior. No
   scalar codec or host sanitizer can establish those facts.
5. Exact page-attribute/permission semantics and any required non-plain table,
   block or additional descriptor fields for the selected firmware. Unknown
   forms remain refusals rather than being normalized or guessed.
6. An independently reviewed kernel lifetime/locking adapter, immutable
   snapshot provenance and real target tests under separately authorized
   criteria. This pure library and its AArch64 relocatable compile are not
   that adapter or a boot authorization.

None of those runtime contracts is weakened by this source foundation.
