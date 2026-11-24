Whole pinned Finch ColorSync implementation (four C files) with our source-derived
declarations and SiliconForest's CFRuntime ABI. Its exact Skia m155 skcms module is
fetched as open source and built locally; no renderer, SDK or Apple profile files.
`fetchzip` hashes the unpacked source because Gitiles gzip metadata varies.

Candidate has 293 own exports and retains all 11 names from the installed thin
ColorSync wrapper. Clean root import scan. The original Linux-built CLI profile/
transform test exits 0 on M1 and exactly matches all 152 result lines on the M5.
The provider is now installed with cold on-target hashes, adjacent `.pre-finch`
originals and passing media/app/compatibility regressions. See the installed
ledger in `../media/INSTALLED.md`.

This changes profiles from the thin wrapper's name strings to actual ICC CF
objects; do not mix old string objects into new APIs. Device preference writes,
external CMM plugins and installation of the source-generated profile files are
not tested. Finch's empty CMM enumeration/code-fragment limitations remain.
Public numeric facts checked against the maintainer's Go API:
https://pkg.go.dev/github.com/deploymenttheory/go-bindings-macosplatform@v0.20.1/bindings/internal/raw/frameworks/colorsync

The first installation trial rolled back after the core update made public
`CC_MD5` return NULL without writing its output. A direct two-vector diagnostic
and owner request are retained for PERF; no core file was changed here. ICC
checksums now use the existing MIT `m1-avd/user/sf_md5.h` privately, through our
small byte-output adapter. It exports no CommonCrypto API or private crypto
context. All 21 ASan/UBSan host vectors (including MD5 padding boundaries) pass;
the unchanged Linux-built M1 client again matches all 152 native result lines.
The public CommonCrypto failure remains a separate unresolved core issue.
