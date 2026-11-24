# Provenance and scope

All new C, C++, Python and documentation here is original. Original-code license
selection remains pending. No upstream implementation was copied. The adjacent
frozen codec is compiled as a dependency, without changes or a copied fork.

The allocator/lifecycle contract comes from local primary source at
`sources/puredarwin-m1-bringup`, pin
`e7aa00a60f0b56ef35aa24252e81467787bf71ea`. Ten selected source and export files
were verified against exact Git objects in
`reports/sprint-010/gpu-uat-adapter-design-001.json`; that report records their
hashes and locations. In particular, IOBMD physical-mask handling bypasses the
system mapper only with `MapperNone`, and nonpageable IOBMD initialization owns
an internal AutoPrepare operation. Private code balances only its explicit pair.

The compiled kernel headers are the existing immutable
`rxi8m8ki35zs9pdj2vnis26wjb1cdfcb-puredarwin-xnu-headers-arm64-0.1` store output,
also identified in the existing trackpad SDK/KC independent-review receipt.
The OSS SDK is `6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5` and the
cross toolchain is `v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix`.
`validation.json` hashes all 258 file dependencies used by this compile. Installed
headers are generated/filtered exports and are not claimed byte-identical to raw
XNU source. No new SDK derivation reproduction or KC build was performed.

Compilation uses the existing pinned `pd_apple_builtins.h` compatibility shim,
with its hash included in dependencies, as the repository's extension build
does. That shim substitutes type-segregation builtins; its source explains the
loss of those allocator-hardening properties. No new private allocator API is
used to bypass the public factory.

The codec source set is
`b174c6022d2d5bf5425dabe77c4b229578ae0fa5c0cb9ea47ee980ed40dde835`.
Its provenance pins the MIT Omacom/m1n1 reference at
`121fcbf35d4501814b58ca5347926a4173b6a512`. This leaf consumes the original
codec, not reference Python. The already acquired T8103 hardware configuration
records 40 output address bits; its hash and partial acquisition provenance are
in the design report. The new 36-bit allocation restriction is an intentionally
narrower admission policy, not a newly inferred hardware format.

Host tests use fake allocations and synthetic graph bytes. Compilation produces
only relocatable objects, discarded after hashing and import inspection. There
is no device, GPU, DMA-runtime, boot, firmware or safe-unload evidence here.
