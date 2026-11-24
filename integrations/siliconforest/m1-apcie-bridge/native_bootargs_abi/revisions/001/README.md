# Compiled KC BootArgs ABI verifier

`verify_kernel.py:verify_kernel(exactbytes)` accepts one immutable `bytes`
object and returns fresh profile/hash metadata, or raises `VerificationError`.
There is no caller ABI assertion, arbitrary profile argument, writer, proxy,
backend or reboot function. The sole production profile is adjacent
`PROFILE.json`; its SHA256 is embedded in code and verified on every call,
before parsing the image. Missing or altered profile refuses. Trusted callers
must also hash-bind the verifier code. Python module privacy is an API boundary,
not isolation against hostile Python code; `_verify(raw, profile)` is private
and exists for tiny synthetic tests only.

The profile binds frozen report3994d145, its retained parser/verifier, declared
standalone58da, selected KCaf9 and native wire codec67b036. The compiled native
1152-byte BootArgs profile is `darwin-xnu-e7-arm64-bootargs1152-r2v2-v1`. Full
same-VA `__TEXT_EXEC` and `__text` byte equality plus UUID bind the prior named
standalone function evidence for the0x478 tail read. No symbol scanning, byte
occurrence count or revision-number inference selects the ABI here. Additional
numeric subtype and relocation fields in the profile were read from the exact
hash-verified selected KC headers. No unique device identity or runtime bytes
are copied into this leaf.

Bounds are256MiB/image,4096 commands/header,1MiB command region,256 sections per
segment,4096 sections/header and256 fileset entries/header. Commands exactly
fill their declared region and have bounded aligned sizes. Strings must have
canonical ASCII/NUL padding; entry names and entry header offsets are unique.
Only ARM64 little-endian64-bit Mach-O is admitted: outer MH_FILESET, one
`com.apple.kernel`, embedded MH_EXECUTE and the exact expected subtype/flags/
UUID. The embedded header is mapped through the outer file segments, starts at
its first `__TEXT` segment and fits its file-backed range. Parsing is bounded
for other load commands, but this is not a general Mach-O loader validator.

The member's exact `__TEXT_EXEC` VM layout/protections/flags/size and all its
section metadata must match the profile. The full executable segment and full
`__text` byte hashes must match. KC segment/section offsets are **absolute in
the whole file**, never relative to the embedded header. Each consumed range
must resolve uniquely through outer VM-to-file coverage; section offsets must
also equal segment offset plus VM displacement. A forged member pointing to a
golden unmapped blob refuses even if that blob has the expected digest. Partial
or ambiguous outer mapping refuses. Unsupported cross-segment coverage refuses.

File-only relocation can pass when entry, member and outer mappings move
coherently while VM layout/UUID/flags/sizes/full code bytes remain identical.
The returned whole-image SHA records that different container; this is a fresh
code-profile check, not reuse of the selected-af9 packet's admission. Other
members, nonconsumed kernel data, loader fixups, signatures and loaded state
are not authenticated by this result. Copied nonexecuting PRELINK placeholder
section extents are structurally parsed but not used as address authority.
Return status is `COMPILED_KERNEL_ABI_MATCH_ONLY` and
`runtime_or_boot_admitted` is always false. This proves no final guest BootArgs
value, allocation, write ordering, successful reboot or explanation of an old
failure. Future writer integration remains separate and opt-in.

Run synthetic tests and compiled Python mutation checks:

```
python3 -B test_verify.py --mutations
```

That command reports actual artifact evidence as `NOT_REQUESTED_NOT_EVALUATED`.
For an explicit check of the saved selected KC:

```
python3 -B test_verify.py --mutations --actual-kc /absolute/path/to/pinned/kernel.macho
```

An explicitly requested missing/unreadable/different cached artifact fails with
exit2; it is never skipped. Tests construct tiny private synthetic profiles,
cover truncation/commands/strings/counts/ranges/overflow/duplicates/UUID/flags,
coherent relocation, absolute-offset mistakes, unmapped golden blobs and outer
mapping ambiguity. Every byte of the synthetic executable is mutated. Python
source mutants must compile and fail semantic assertions; syntax/import/test
errors are not counted as kills. The separate actual saved-KC check invokes
the production entry and rejects in-memory first/last executable-byte changes.
Neither test writes an artifact or executes target code.

For source-bound loading, a caller may read/hash the module, compile those same
bytes into a module with `__file__` set to its exact path, then call
`verify_kernel` on the exact selected immutable KC bytes. The sibling profile
is checked on every call. No host, controller, m1n1 source, old capture, packet
or grade is changed by this leaf. Hardware STOP and GPU HOLD remain in effect.
