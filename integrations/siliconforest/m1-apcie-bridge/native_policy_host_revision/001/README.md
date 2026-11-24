# Isolated private-policy host proposal

This SOURCE-ONLY before/after proposal starts from host002's after tree
(hostf549dec1/controller0ce772dc), with unchanged frozen helper files copied into
both trees. It does not apply to canonical/M5 files, deploy a packet, open a
transport or permit a boot. The proposed probe
`org.darwinlab.driver.PDBCMPrivateProbe` and fixed command
`/usr/bin/darwin-native-private-test` are design names. No matching probe/command
artifact is built or admitted here. Actual current af9 lacks the member and is
correctly refused by this mode. Synthetic gate positives are explicitly fake.

The exact unique-key build-record field is
`native_policy_experiment={version:1,producer_profile:
"t8103-pinned-hv-source-facts-v1"}`. It requires the existing exact native1152
wire opt-in and hash-bound RAM root; every other `*_experiment` field is refused.
Reported and raw fileset membership must name the design member exactly once.
Presence proves no implementation or ABI for that future member. The existing
production ABI verifier still checks the actual selected kernel executable
bytes. No caller-supplied ABI-success assertion is accepted.

Missing mode keeps previous BootArgs, placement, console and recovery behavior.
The private branch appends only `pdbcmprivate=1`, generates an early fresh policy
nonce independent of ConsoleProof's existing nonce, and uses that policy nonce
for source capture, producer context and the one fixed guest command. The UART
writer permits only optional /bin/sh, the unchanged split console challenge and
that command, then seals before the attempted send. Guest collector error text
is retained as raw diagnostics; it does not end reading or authorize more input.
The unchanged `command_line_observed` field remains CLI proof only. There is no
private-result parser/qualification or built collector protocol in this proposal.
Only this exact mode disables optional framebuffer diagnostics; the legacy
capture class and all other modes retain their previous path.

Controller verification and worker verification rehash the new integration helper
and every frozen source dependency before target reboot/opening. The controller
snapshots exact bytes and rechecks hashes before each source snapshot. New modules
execute bounded bytes just read+hashed, not an exec_module reread. Source files
are trust bindings, not hardware permission. Source snapshots include immutable
C MCC profile files although no C code executes on the host. Unknown future
observer/collector hashes are not pinned or invented.

After actual collection and the existing J293/T8103 preflight, the private path
binds the exact original ADT and original736 BootArgs. It selects only MCC reg1
from the original tree. Because pinned get_reg translates a base only, an original
bounded raw-property check independently requires the whole aperture to fit
exactly one parent range under fixed2/2 cells and no root translation, then
compares its result with get_reg. No guessed physical base is accepted. The
frozen one-shot collector issues exactly the96 read32 plan offsets, journals
attempts and retains bounded prefixes/full384 bytes. Exceptions and aborts keep
prefix evidence; a backend latch prohibits retry/reentry. All full bytes are
validated and the enabled slot list (including multiplicity) must exactly match
the bounded old compact table. That table is only a crosscheck; it cannot replace
the full sample or imply global exclusions. This source derivation does NOT
establish actual MMIO mapping/read safety or quiescence.

Original capacity and reservation are unchanged: no resize, modified u.ba,
new memory high end, or m1n1 source/bootloader change. Before load_raw the mode
checks current serialized base size + a conservative allowance for seven fixed
16-byte memory-map descriptor headers + maximum facts/property bytes against
the original aligned capacity. Final payload staging retains old ADT/TrustCache/
RAMDisk/top placements, measures the exact final size before native encoding,
and refuses if final bytes exceed that same original limit. Source/fit checks
precede new policy payload writes. Initial load_raw legacy BootArgs serialization
is preserved; the entire path still limits command bytes to607 plus NUL.

The producer builds the final native bytes and exact facts/handoff plan. A
per-instance SealedADT read view delegates underlying tree operations but checks
its exact base serialization before returning only those planned final bytes.
The original pinned HV.start runs unchanged: essential mappings/page updates,
ADT build, original capacity assertion, ADT upload, framebuffer shutdown and
seven-word start. A mode-only transport guard recognizes the build/upload phase,
requires exactly one expected address/payload, journals the attempt before write,
performs an exact bounded readback, and saves separate ACTUAL upload/readback
files. Planned bytes alone are explicitly not actual-upload proof. Further writes
overlapping any byte of the whole reserved ADT capacity refuse. Existing utility
instruction-buffer writes outside it are delegated, not introduced or authorized
by this proposal. The private jump guard requires completed actual upload/readback,
unchanged pointers/view/base and one matching entry/BootArgs call; the inherited
seven-word start guard remains. Any uncertainty consumes the attempt; no retry.
These boundary checks do not make globals immutable inside arbitrary callbacks.

All backends, serial objects and targets in executed tests are fake. One test
executes the extracted genuine HV.start body with fake callees to preserve exact
ordering. Historical MCC translation replays saved bytes only, without printing
raw values, acquiring sources or changing old grades. Initial test fixtures had
an invalid overflow expectation (16 bytes still rounds to16KiB) and omitted a
fake transport dev attribute; those fixture failures appeared in tool output and
are not claimed as saved contemporaneous logs. The corrected final tests pass.
No new helper mutation-coverage claim is made by this source freeze.

Run `python3 validate.py --output-dir /private/scratch/path` for isolated checks.
No apply/deploy/actor entry point is supplied. A future packet still needs actual
probe/collector artifacts, exact full review, source/identity admission and its
mandatory verified Linux recovery. Hardware STOP003 remains unchanged.
