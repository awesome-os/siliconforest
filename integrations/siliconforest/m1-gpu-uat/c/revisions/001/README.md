# T8103 UAT codec, C

Original scalar codec for T8103 TTBR and PTE words. The numeric oracle is `../revisions/003/uat_codec.py`. Revisions 001, 002, and 003 are unchanged.

The only accepted format is `t8103`. A physical address must be below 2^40. A page or table is 16 KiB aligned, and a block is aligned to its level span. A virtual address is the low 40 bits, or that same field sign-extended through bit 63. Level 0 has two roots selected by bit 39. There are 64 contexts, each a 16-byte TTBR pair. ASID is 16 bits. Reserved descriptor bits are rejected.

`uat_refuse_block` records the raw descriptor output, the span, and the virtual offset inside that span. It does not write a translated physical address. This directory has no table walker, allocator, DMA, MMIO, or firmware handoff. On an error, output arguments are left unchanged.

License selection for this original code remains pending under AGENTS.md. `PROVENANCE.json` records the MIT m1n1 pin and the Linux numeric crosscheck. No upstream function was copied.

Run the host unit test, the `NDEBUG` guard, and the seeded ctypes differential with:

```sh
python3 test_uat_oracle.py
```

Build products stay in a temporary directory. The result is software preparation only.
