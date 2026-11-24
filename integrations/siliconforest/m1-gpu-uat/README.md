# UAT page-table codec

This directory encodes and checks UAT page-table descriptors as ordinary
numbers and byte strings. A passing test is software encoding evidence. It is
not a GPU translation, a mapping, an invalidation, a firmware handoff, or a
driver. Nothing here touches a device, a candidate, or the HID work.
`m1-dart` is a separate codec and is not used.

Run the host checks from this directory:

```sh
python3 -m unittest test_uat_codec.py
```

## Accepted formats

| Format | Where it comes from | Virtual width | Physical limit |
| --- | --- | --- | --- |
| `t8103` | m1n1 level-0 bit 39, the path below G15. T8103 is G13. Linux `uat_ias` is 39 bits per root and `uat_oas` is 40. | 40 bits. Bit 39 selects the root. A sign extension of that bit is accepted; every other high pattern is rejected. | below 2^40 |
| `g15` | m1n1's single `G >= G15` path, level-0 bit 42. The matrix names covered by that path are G15 and G15X. The name `g15x` is not an alias. | 43 bits, with the same sign-extension rule around bit 42. | the 48-bit descriptor field only. No output width for this path was in the acquired sources. |

Any other format name is rejected. G14 and G14X use the same level-0 bit as T8103 in m1n1, and this codec does not assign them T8103's 40-bit output limit.

## Descriptor rules

Level 0 is a 16-byte pair of TTBR words. A context is `ctx * 16` for `ctx` in 0..63.
Level 1 has 8 entries on `t8103` and 64 on `g15`. Levels 2 and 3 have 2048 entries, 16 KiB each.
Table and page addresses are 16 KiB aligned. A level-1 block is aligned to 64 GiB and a level-2 block to 32 MiB.
Bits that the m1n1 descriptor classes do not define are reserved and rejected.
A nonzero word with the valid bit clear is rejected. The all-zero word is the only empty descriptor.
Level 3 accepts a page (`TYPE=1`) and rejects a block. Levels 1 and 2 accept a table or a block.
A page translation adds the 14-bit offset inside the page. A block translation returns the descriptor address unchanged and reports the span.

Pins, file hashes, and the choices that were not adopted are in `PROVENANCE.json`.
Original-code license selection remains pending. Upstream files keep their own licenses.
