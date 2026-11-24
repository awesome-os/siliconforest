# Development increment 001 — November 7, 2026

## Implemented

A local Git repository with the previous scaffold as its first commit. Added:

- A bounded, freestanding C buffer-lifetime reference policy, with generation
  tokens, typed leases, client-death handling, timeout quarantine, ordered
  quiescence acknowledgement and conservative reclamation.
- Python source acquisition/byte verification with explicit network opt-in,
  a lexical Rust dependency inventory, C enum comparison, C layout-probe
  generation, layout-result comparison and a unified local test runner.
- A private-repository publication helper with dry-run default and account,
  branch, worktree, remote, visibility and commit verification.
- Read-only-token CI for local tooling/contracts, a porting map, evidence
  records and local issue drafts.

## Executed and recorded

See `reports/checks.json` for the precise host/compiler and commands. The final
local run passed 81 Python tests with no skips and 16 native C test groups under
AddressSanitizer and UndefinedBehaviorSanitizer. One C group performs 100,000
deterministic adversarial state transitions. The policy also compiled as a
freestanding object. The Python suite compiles and runs a synthetic layout
probe to test the generator.

Separately, the complete PureDarwin DRM interface header was byte-verified and
a Linux-host layout probe was compiled and executed against it. Two source
prefixes were inventoried and the 12 observed Asahi enum values compared. Each
report records its narrower scope.

## Not executed / not implemented

No GitHub repository creation or push, no GitHub Actions run, no direct-network
source acquisition test, no complete upstream build, no Darwin/ARM64 ABI probe,
no XNU kernel integration, no device access, no physical boot, and no work on
the filesystem under recovery. GitHub publication tests use mocks.

The connected account was confirmed as `chriskearney`; the session's GitHub
connector provides read operations only. The browser tools also lack write
controls. No credentials were requested or extracted. The ZIP includes local
Git history; the publisher can create the private remote from an authenticated
healthy workstation when invoked explicitly.

## Next work, in order

1. Capture and byte-verify complete selected sources on a network-capable,
   healthy workspace. Expand the dependency inventory past its two prefixes.
2. Run independent complete Asahi header layout/encoding probes using the real
   kernel/Mesa headers and each intended SDK. No successful stub substitutions.
3. Review a minimal XNU backing-store adapter against the executable lifetime
   policy, including last-map callbacks and firmware-held pages.
4. On the restored Linux builder, establish the unmodified PureDarwin ARM64 VM
   baseline before adding hardware changes. Keep the M5's known-good debug
   procedure intact for the later target runner.
