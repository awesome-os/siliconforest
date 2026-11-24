# Pure guest metadata handoff planner

This original helper inserts the fixed `darwin-lab-platform-facts` property in
supplied guest ADT bytes. It returns immutable bytes and binding metadata. It
has no device writer, proxy, loader object, registry API, allocation operation,
source collector, or conversion to owner configuration.

The caller must already have a finalized guest layout, genuine fresh source
facts, sufficient separately reserved ADT capacity, and final native BootArgs
bytes. `planned_size()` gives the exact future ADT length before encoding those
BootArgs. Insertion adds a fixed property header plus a fixed-length facts wire;
the six hashes have fixed widths. Their values therefore do not change the
planned length. `plan()` checks the supplied final BootArgs size, translated
ADT address, managed envelope, conservative boot prefix, ADT/BootArgs descriptor
bounds and non-aliasing, and known source exclusions. Both whole allocations
must end before actual `topOfKernelData`; a larger declared policy reservation
cannot substitute for that live boot boundary. No live allocation is resized.

The base ADT digest is the hash of all original bytes. On a serialized tree
containing the one new property, `canonical_base()` removes exactly that
property and decrements its parent's count, preserving every other byte,
including property order and padding. The final ADT hash remains external to
the property. This defines a non-self-referential preimage; it does not
authenticate a producer or validate a removed payload.

The bounded ADT parser requires complete trees, unique property names and node
paths, NUL-terminated 32-byte property keys, and full unsigned 32-bit property
lengths as XNU consumes them. It does not mask a firmware/template bit. Caps are
2MiB, depth32, 4096 nodes, 4096 properties per node and 65536 properties total.
One available property slot is required before planning insertion. Existing
facts properties are refused; there is no implicit replacement or migration.

Frozen facts and BootArgs codecs are SHA-pinned and compiled from the exact
bytes read. Their hashes are checked again at each plan. Facts expected labels
must match; actual supplied base ADT and final BootArgs bytes must match their
two corresponding labels. The four other identities and source digests are
trusted context declarations here. This helper does not derive source
inventories, prove global exclusion coverage, establish boot-phase quiescence,
or attest that supplied bytes were actually written/read back on a target.
Canonical 1152-byte decoding is an explicit profile, not proof that an
indistinguishable zero-tailed legacy blob was produced by the new writer.

The mutable pre-freeze independent review found two real parser mistakes:
accepting an unterminated property key and masking a high length bit. Exact
pre-fix bytes and reproductions are preserved privately. Both are corrected
before freezing this leaf; their reintroductions are assertion-killed mutants.
An initial mutant harness also misconstructed a chained-comparison deletion;
that failed experiment is preserved and is not counted as a semantic kill.

Run `python3 validate.py --output-dir /new/private/path`. Output directories are
created exclusively, so reruns cannot overwrite the initial validation logs.
The saved-ADT audit tests parser/splice byte fidelity on historical local bytes
only. It cannot qualify a new handoff because that capture has no separately
saved final native BootArgs wire. No old capture or grade is modified.

Future deployment still requires an admitted trusted producer, source binding,
integration with the selected loader's final serialization, actual dump-back,
and a separately reviewed packet and hardware decision. This source supplies
none of those permissions.
