# Source identity and evidence levels

The archived audit is historical. `config/source-registry.json` deliberately
retains candidate/unverified entries. It is not promoted by matching an enum or
successfully compiling our own tests.

## Rebuilt-host observations

New evidence is recorded separately from the inherited registry. The rebuilt
host fetched and byte-checked 12 selected PureDarwin files, acquired an exact
clean PureDarwin checkout and its corecrypto gitlink, and evaluated the pinned
flake with Nix 2.34.9 without changing its lock file. See
`reports/host-source-verification-2026-11-07.json` and
`reports/puredarwin-baseline-inputs-2026-11-07.json` for those successive scopes.
The selected source trace in `reports/sprint-005/pinned-source-review.json`
records full-file hashes independently of its narrower semantic review.

`reports/primary-commit-observations-2026-11-07.json` records remote commit,
signature-status and root-tree observations for the three primary Omacom pins.
It does not establish recursively verified local trees or builds for them.
The acquisition and execution limits below describe the earlier increments;
they must not erase or silently promote the newer observations.

## What acquisition verifies

`tools/acquire_sources.py plan` does no networking. `fetch --allow-network`
fetches selected files from commit-addressed GitHub raw URLs, rejects redirects,
limits response size, and matches each body to the expected Git blob SHA. The
expected blob is inherited from the audit manifest. Its receipt also computes
SHA-256. It never executes source or overwrites a mismatching cache entry.

This is byte verification, not commit-signature verification, a full Git tree,
submodule verification, toolchain pinning or a complete reproducible build.
The write cache is for a trusted local workspace, not a sandbox against a
malicious concurrent filesystem writer. No tokens or cookies are used. Private
repositories may require a separately reviewed acquisition path.

The development container could not make direct outbound source downloads.
Network acquisition was therefore unit-tested with injected byte transports,
not tested end to end. Repository reads used the connected GitHub service.

## What was independently byte-checked now

The complete PureDarwin `pd_dri_cdev.h` was fetched through the connector, copied
into the local analysis workspace, and matched to Git blob
`395083ab8450653ede86772e27e1d3eff4e4b759` before compiling a host probe. The capture
is not included in the distributed repository. See
`reports/pd-header-verification.json` and `reports/pd-dri-layout-linux-host.json`.
To reproduce, explicitly fetch that selected header and compile the generated
probe with its directory on the include path. Do not relabel this host result
as a Darwin ARM64 result.

## Excerpts versus full files

`reports/source-inventory.json` records two scanned source prefixes. Their
SHA-256 values identify the captured prefixes, NOT the full upstream files.
Full-file blobs reported by GitHub are recorded separately. The enum observation
is a normalized semantic transcription from two independently fetched header
excerpts; it is not a full-header byte comparison.

## Required before a real build lock

Verify complete commit trees and recursive submodules, toolchain/Nix inputs,
firmware source and distribution constraints, exact target identity, host/proxy
pairing, and a repeatable unmodified build. Then introduce a new lock schema
with real evidence, rather than changing old booleans to imply verification.
