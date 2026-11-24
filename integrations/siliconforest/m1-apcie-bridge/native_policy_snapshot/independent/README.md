# Independent synthetic driver

`python3 -B independent/check.py` compiles `runner.c` with the actual production
C decoder and frozen MCC decoder under host ASan/UBSan, then sends an original
synthetic wire corpus through that binary. Run from any directory. Optional
`--output PATH` writes the result metadata; it does not freeze the inputs.

The driver packs fixtures directly without using the production encoder or its
fixtures. Python/C acceptance and all returned ranges, original RAM boundaries,
source descriptors, records, MCC slots and union are compared. Accepted fixtures
also roundtrip through the production encoder. The C runner independently checks
preservation of raw MCC, expected identities, declared source digests and profile
fields, unchanged inputs, and bit-for-bit unchanged output on every rejection.
It exercises all six pairwise aliases at equal and partially overlapping
addresses, plus four null arguments. Workspace/output are static or heap owned.

The third interval oracle uses connected components of touching/intersecting
source intervals, separately from the implementations' sorted merge. It filters
relevance against each original RAM interval and retains full source boundaries.
2,048 generated interval cases include adjacency, holes and capacity refusal.
The corpus also tests every bit of one wire, every short-header length, all
expected identity bytes, source-label mutations, source-class empty/nonempty
semantics, MCC sparse slots, 256/257 records, union8/9, physical overflow,
remap exclusion from the physical union, boot crossing adjacent original RAM
and retained full exclusion boundaries.

The Python implementation is a parity reference, not an independent oracle for
all grammar rules. In particular, matching implementations alone cannot prove
source authenticity or source completeness. Positive opaque-source-label and
syntactically empty-class cases deliberately preserve that distinction.

This is immutable-input, caller-serialized host testing. It does not simulate
concurrent mutation safely, acquire any source facts, execute native APIs, or
prove a usable memory policy. All physical-looking numbers are fabricated.
`result.json` binds the exact inspected moving production bytes; it is development
validation, not independent approval of a later frozen production manifest.
`validation.json` freezes only this independent driver and its recorded results.
