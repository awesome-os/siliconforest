# Compile-only target checks

```sh
python3 tools/cross_abi.py
python3 tools/check.py --sanitize --cross
```

The tool generates a constant array of `sizeof`, `_Alignof`, `offsetof` and
selected integer constants. Clang evaluates those expressions for each target
and emits LLVM IR. A strict parser extracts only literal constants from the
single expected global. No foreign executable, emulator, SSH or hardware is used.

Default targets:

- `arm64-apple-macos11`: ARM64 Mach-O relocatable objects.
- `aarch64-unknown-linux-gnu`: AArch64 ELF relocatable objects.

Default coverage: 10 original C types, 42 field offsets, their sizes/alignment,
and 3 constants. The tool also compiles both the lifetime policy and the new
backing-store coordinator to actual target object files and checks their header
identity. The object check is not a complete Mach-O/ELF validator.

Clang's builtin freestanding headers supply C integer and size types. No Darwin
SDK is fabricated. **This is not a PureDarwin SDK build, Darwin kernel extension,
Asahi UAPI comparison, linked application, or executed ARM64 test.** A separate
future test must use the real kernel/Mesa headers and the intended SDKs. Plain
ARM64 does not demonstrate arm64e support or XNU build/link integration.

The current Mach-O object symbol listing contains unresolved policy references
and compiler helpers including `bzero`, `memcpy` and `memset`. Freestanding
compilation does not eliminate link-time runtime obligations.

The parser was independently cross-checked against the existing executable
layout-probe generator on a complete, hash-matched PureDarwin `pd_dri_cdev.h`.
Both probes compiled with real **Linux x86-64 host headers**, and the executable
host probe produced the same selected values. This validates the extraction
method at that host boundary, not the Darwin/ARM64 layout of that header.

## Records

`reports/sprint-002/cross-abi.json` records commands, target metadata, measured
values and source/object hashes. `pd-host-probe-crosscheck.json` records the
independent host method comparison. Compiler execution is explicit; there is
no source download or target launch in these commands.

## Primary format/tool references

- Clang cross compilation: https://clang.llvm.org/docs/CrossCompilation.html
- Clang freestanding behavior: https://clang.llvm.org/docs/UsersManual.html
- Apple Mach-O header: https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/loader.h
  (header definition and MH_OBJECT inspected; blob at inspection:
  `4d1b930c3dcee476538d4cb022e3cffb2d3a50e2`, lines 35–145; not vendored)
