# Finch CLI conformance subset

Build on Linux with `nix-build --no-out-link
integrations/siliconforest/finch/tests/suite.nix`. Copy the output tree to an unused
directory under `/private/var/db/sffinch/` on SiliconForest. All programs are
compiled by our Nix toolchain using our declarations and SiliconForest's open
headers. No Mac build, SDK headers or Apple binaries enter this build.
The bundle rewrites our test programs' PD Foundation/CoreFoundation library
names to the public framework paths (also present on SiliconForest), restores
their ad-hoc page hashes, and records input/final SHA256 in `binary-hashes.json`.

From the copied directory, capture M1 execution:

```
python3 tools/compare-tests.py capture manifest.json --role candidate --output candidate.json
python3 tools/compare-tests.py compare candidate.json --masks tools/test-masks.txt --exceptions tools/host-test-exceptions.txt --output comparison.json
```

Missing reference receipts produce BLOCKED and a nonzero comparison result.
Finch's three `.expected` checks compare directly. An exit code of zero alone
does not establish comparison conformance. Failed exits, timeouts, differences
and expected failures remain failures. Stdout and stderr are preserved separately;
only stdout is compared, as in upstream `tools/host-tests.sh`.

The owner authorized running reviewed Linux-built reference programs on the M5.
Record test names, hashes and outcomes in `runs/m1-coord/requests-for-CLAUDE.md`.
Copy only our programs to a fresh /tmp directory, verify SHA256 remotely, enforce
30 seconds per program, and delete the directory. No sudo, installs, Mac builds,
DYLD overrides or Apple files copied back. Accounts service mutation/access tests
are excluded from reference execution.
Use the exact Linux-built binaries on both machines. Reference capture requires
explicit test names and removes DYLD overrides; it cannot build or contact SSH.
Return its receipt to Linux and pass `--reference reference.json` to `compare`.
The harness rejects different binary hashes. No reference output is invented.

This is the non-GUI subset of Finch's `userland/tests` and the capture/diff policy
from `tools/host-tests.sh`, using its pinned masks and exception reasons. Tests
that print a loaded-image line discard exactly that line; numeric/value tests
compare every line. It does not launch Finch's window server, use system_profiler,
exercise cross-store CoreData, compile Swift, or claim to port the entire suite.
Blocked source ports and unselected tests remain outside this initial manifest.

The expanded bundle contains 21 CLI programs, including all eight numeric groups,
ColorSync and the staged CloudKit getter correction. Three pinned expected checks
and four assertion-only probes have separate classifications. Raw differences
in CFNetwork, CloudKit formatting, LA superclass and Sparse FGMRES remain visible;
the Accounts reference remains BLOCKED. See the handover table for actual results.
