# UAT page-table codec, revision 002

This revision encodes and checks the acquired T8103 UAT profile as ordinary
numbers and byte strings. A passing test is software encoding evidence. It is
not a GPU translation, a mapping, an invalidation, a firmware handoff, or a
driver. Nothing here touches a device, a candidate, HID, or `m1-dart`.

Revision 001 is frozen. Its files are the parent directory and the byte-identical
copies in `../001/`. This revision does not edit them.

Run the host checks from this directory:

```sh
python3 -m unittest test_uat_codec.py
```

## T8103 profile

| Item | Accepted value |
| --- | --- |
| Level-0 selector | bit 39 |
| Per-root input | 39 bits |
| Output address | below 2^40 |
| Granule | 16 KiB |
| Level 1 / 2 / 3 | 8 / 2048 / 2048 entries |
| Page result | `pa` is the page output plus the offset inside that page |
| Block result | translation is refused. `raw_output` and `span` are the descriptor facts. `virtual_offset` is not added. There is no `pa`. |

`g15` and every other generation name are rejected. The m1n1 `G >= G15` level-0
bit and its level-1 entry count are provenance facts only. No output width was
acquired for that branch, so the 48-bit descriptor field is not an output limit.

The pinned walker does not add `(virtual & (span-1))` to a block output.
`recurse_level` reports the virtual span separately from `PTE.offset`.
`iotranslate` adds only an in-page offset and does not stop on a block.
Returning the block base as `pa` would be an incorrect translation.
