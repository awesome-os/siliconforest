# T8103 UAT codec, C

Original scalar codec for T8103 TTBR and PTE words, C revision002. The numeric oracle is `../revisions/003/uat_codec.py`. Python revisions001/002/003 remain unchanged. All six frozen C revision001 files, including their original provenance, are preserved byte-for-byte in `revisions/001/`.

The only accepted format is `t8103`. A physical address must be below 2^40. A page or table is 16 KiB aligned, and a block is aligned to its level span. A virtual address is the low 40 bits, or that same field sign-extended through bit 63. Level 0 has two roots selected by bit 39. There are 64 contexts, each a 16-byte TTBR pair. ASID is 16 bits. Reserved descriptor bits are rejected.

`uat_decoded.has_pa` reports only presence of the decoded `pa` field: a next-table base for TABLE or an output-page base for PAGE, including a valid zero base. No VA offset is added, and no address translation is established. It is zero for EMPTY/BLOCK. The one-byte field replaces `translated` at offset 46; the decoded record stays 48 bytes. The implementation is byte-identical to C001 except for those six identifier substitutions; the host suite verifies this directly.

`uat_refuse_block` returns `UAT_REFUSED` after writing the raw descriptor output, span, and virtual offset to `facts`. It leaves the separate `pa` output unchanged. Any `UAT_ERR_*` leaves output arguments unchanged, including both outputs of this function. Callers must supply valid, sufficiently sized buffers; `pa` and `facts` require disjoint storage. This directory has no table walker, allocator, DMA, MMIO, or firmware handoff. The byte-store helper serializes ordinary memory; it is not atomic live page-table publication or cache maintenance.

The header supplies C linkage guards for C++ callers. Compile the implementation as C and include the header normally from C++; no external include wrapper is needed. No SDK or kernel integration is qualified by the host test.

License selection for this original code remains pending under AGENTS.md. `PROVENANCE.json` records the MIT m1n1 pin and the Linux numeric crosscheck. No upstream function was copied.

Run the host assertions with ASan/UBSan, the `NDEBUG` guard, seeded ctypes differential, archive/numeric-source checks, and mixed C/C++ linkage test with:

```sh
python3 test_uat_oracle.py
```

The same C++ caller is compiled against the preserved C001 header as a negative control: compilation must succeed, but linkage to the C object must fail on a mangled codec symbol. The current header must link and execute a real scalar encode/decode and block-refusal check. The runner also checks PA presence at a zero base, nonzero VA offsets, and explicit expected error classifications. `PROVENANCE.json` binds the source and archive; `source-validation-002.json` records the actual check result. Build products stay in a temporary directory; leak detection is disabled for the restricted host while ASan/UBSan remain active. The result is software preparation only.
