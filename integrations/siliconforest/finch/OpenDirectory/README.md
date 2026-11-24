Whole Finch OpenDirectory Objective-C and nested CFOpenDirectory C library;
account-store/password-hash backend contained inside the new framework tree.
Own declarations; numeric ABI facts checked against the binding author's
[objc2-open-directory 0.3.2 source](https://docs.rs/crate/objc2-open-directory/0.3.2/source/src/generated/CFOpenDirectoryConstants.rs).
Fix the upstream match validation: Contains (0x2004) follows EndsWith (0x2003).
Clean import scan; M1 ObjC/CF read-only probes pass staged and installed.
No account mutations/password verification or setuid helper installation tested.
`declarations.py sources/finch/userland/OpenDirectory` regenerates own headers.
The read-only original ObjC query output matches native macOS with identical
Linux-built binary bytes. A staged metadata correction sets both public library
current/compatibility versions to 1, matching Finch's build. The initial installed
files remain unchanged pending coordinator integration.
