# BCM4378 fake DART map: software tables, not initialized hardware

This original slice reuses the unchanged original `m1-dart/dart_codec.c/.h`.
It builds one validated T8103/T8020 16KiB software page-table image and performs
freshly gated software address lookups. It is not an IOMapper or DMA backend.
There is **no DART register operation**, TTBR publication, TCR enable, stream
invalidation, cache-maintenance acknowledgement, map adoption, device transaction
or quiescence claim. No new hardware register-order implementation was needed.
The frozen codec's exact provenance remains part of the dependency record.

The MIT map is exact m1n1 pin121fcbf35d4501814b58ca5347926a4173b6a512,
`proxyclient/m1n1/hw/dart8020.py`: PAGE_BITS14, two11-bit table indices at25/14,
four TTBR slots, T8020 entry format. The original frozen codec deliberately
supports one canonical32-bit IOVA/36-bit PA profile, slot0, full16KiB leaves,
explicit R/W permissions and <=256 leaves. This wrapper does not copy m1n1
initialization or its bypass stream behavior. No GPL source is read or copied
for this slice. The codec's earlier numerical cross-checks are inherited as
documented facts, not a claim that its provenance was exclusively MIT.

The exact fake endpoint requester is RID0x100 (bus1/dev0/fn0). SID0..15 is an
explicit synthetic binding, not an observed WLAN DART stream. Policy must use
the frozen APCIE IOVA range[0x100000,0xbff00000). MSI16KiB page0xffffc000 is
excluded. RAM and reservation ranges are explicit caller-owned snapshots.
Table physical addresses are labels only, never converted to host pointers.
Root/leaf tables and data must be aligned, in permitted RAM, nonoverlapping
with reservations and each other; the frozen image validator enforces canonical
reserved bits, table references and the leaf cap. No roots are adopted.

`bfd_init` records metadata without modifying table storage. `bfd_map_pages`
supports one contiguous mapping, at most256 pages within a single32MiB L1 span.
It stages both16KiB pages in separate caller-owned workspace, validates the
whole image, checks the fake link again, and only then copies both output
pages. There is no append, unmap, table reuse, allocation or automatic retry.
Caller serializes all access and keeps map/accessor/policy/storage/workspace
stable and disjoint. This model does not promise atomic physical DART updates.

Every init/map/translation entry checks fake RUN/LINKSTS and endpoint PCI
14e4:4425 through the existing checked BCM accessor. Down-link entry refuses
before workspace/table changes; translation output is unchanged. Software
lookup also checks the exact RID/SID, mapping presence and access permissions,
and revalidates the committed image. A late link loss after staging prevents
output-table commit. Read errors become sticky faults, not silent recovery.

**Fake PHY stays reset0x80.** The synthetic LINK_UP state is independent test
input, deliberately not an electrically consistent hardware simulation.
No PHY or PMGR read/write is part of this wrapper. It establishes no normal-DART
MMIO reachability while PHY is reset. The old real survey refusal and its
conservative hardware guard are untouched and were not rerun.

Only `BFD_FAKE_DEVICE` plus `APB_HOST_TEST_ACCESSOR` enables the software model.
KERNEL+fake and the shared PHY/PMGR write escape macro are compile-refused.
Default/KERNEL APIs return BFD_GATED with no I/O or buffer changes. GPIO152 and
SMC radio command paths remain compiled out in every fixture. No firmware,
current-chip identity receipt, port packet, SDK/kext/KC or boot state is changed.

Run `python3 integrations/puredarwin/m1-apcie-bridge/test_bcm4378_fake_dart.py`.
ASan/UBSan cover fake/default/host-KERNEL, zero-write down-link init/map/lookup,
late link loss, exact requester and permission failures, MSI/overflow/unaligned/
reserved/table aliases, upper-aperture mapping,256-leaf cap, damaged reserved
PTE bits and all12 link-read failure boundaries. Twelve compiled assertion-killed
mutants and negative builds are included. Leak detection is disabled for the
host environment. Passing these tests is software evidence only, never boot
admission, DART isolation, actual stream ownership or DMA readiness.
