# Provenance

All implementation and tests are original; original-code license selection is
pending. No external JSON library or copied parser is used. Python's independent
standard JSON parser is a host-test oracle only, with explicit duplicate-key,
ASCII and integer-range admission added independently of the C implementation.

The path/ownership/limits are the approved new desktop-service contract, not an
inference from arbitrary recorded text. The API accepts conservative integer
JSON because the admitted app/client records use integer millisecond/counter
fields. Unknown fields cannot bypass duplicate-key/type/syntax checks. Production
filenames remain a fixed caller policy to accommodate app result/child/PTY/grid/
application records and the new native-client schema without mutable dispatch.

Compilation uses the existing immutable OSS SDK and ARM64 cross toolchain.
Exact source/header dependencies, flags and object hashes are in validation.json.
The Darwin feature-test namespace is explicitly `_DARWIN_C_SOURCE`, so actual
SDK declarations provide NOFOLLOW and nanosecond stat members. No numeric flags,
stat offsets or opaque ABI structures are guessed. Direct object imports are
checked against exports of retained libSystem SHA256
`9fc03c87e929310d881bd5b440bf0f3d7f0b230d79521b07cea539cf5c7e481e`
and its associated libdyld. Objects are not linked into a guest or executed.

Tests inject a separate temporary root/current UID only under `RG_TEST_ONLY`;
production uses literal `/` and UID0. Deterministic post-read race hooks are also
test-only and absent from production objects. Actual host file operations test
the guards without modifying `/private` or any target image. UID refusal is
tested by expecting a different UID on that real temporary tree; the test does
not claim it changed file ownership to another user. File-descriptor counts
before/after each guarded fixture check ordinary cleanup, not close-error or
kernel-I/O quiescence under every failure.

Metadata checks cannot distinguish every write-and-restore race or prove there
are no open writers. The parent must supply actual closed-writer/process evidence.
The record guard does not weaken that requirement or infer success from paths,
PIDs, mode bits, parser acceptance or host tests.
