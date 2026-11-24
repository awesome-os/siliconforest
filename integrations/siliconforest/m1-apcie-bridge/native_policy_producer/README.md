# Copied boot-source facts producer

This original pure host helper derives the six facts classes used by the frozen
native snapshot codec and plans one guest-copy property insertion. It reads no
device, modifies no loader object, allocates no target memory, and invokes no
kernel or owner API. Its immutable input bytes must come from a separately
trusted fresh same-boot capture. The nonce and selected KC/ramdisk/HV digest
labels do not authenticate that capture or establish global exclusion coverage.

The exact supported profiles are named in `producer.py`. Original BootArgs are
explicitly the pinned m1n1 R2/V2 736-byte source; only its common envelope and
six 64-bit Video fields through byte88 are interpreted. The rest remains opaque
and is hashed in full. Final native BootArgs are canonically decoded by the
frozen 1152-byte codec. Numeric R2/V2 and matching lengths alone do not establish
which writer produced a blob; a zero-tailed legacy representation may be
indistinguishable. The caller supplies the provenance.

`measure()` validates the copied ADT inventory, original envelope/video, and
complete 384-byte MCC snapshot before returning exact facts/ADT byte counts.
The caller can then finalize a separately reserved layout and encode native
BootArgs with that ADT length. `produce()` repeats source derivation, checks the
pinned HV's preserved envelope high and non-expanding lower boundary, preserves
the original/native video footprint fields, derives a conservative page-rounded
boot prefix, encodes facts and invokes the frozen pure handoff planner. It
never grows an allocation or silently updates final BootArgs to make a plan fit.

Class1 retains every enabled original MCC slot and all384 raw bytes; coverage
remains four planes/four TZ slots only. Class2 retains every pmap record and its
WIMG/signature. Class3 is exactly the firmware-declared `/vram/reg` reservation,
which must contain the nonzero, four-byte-aligned original visible footprint
inside chosen DRAM. Whole VRAM containment is an explicit constraint of this
T8103 RAM-framebuffer producer profile; the generic raw inventory can report
other numerical extents. A generic MCC cover or visible-only range is not
substituted for the allocation record.

Classes4/5 retain all physical/remap observations, including duplicate rows and
zero numerical remaps, in deterministic source order. Opaque IOVA fields remain
in the returned private inventory without physical arithmetic. Class6 covers
the full native boot prefix, rounded outward to16KiB. Original RAM is exactly
the supplied native BootArgs envelope, never the larger chosen DRAM. Kernel
boot-argument memory limits may reduce its later VM use; this producer does not
measure the live VM allocator. All observations must fit256 records, and the
downstream exact relevant union must fit8; no truncation or inward clipping.

Source digests are recomputed here using explicit tagged framing of the supplied
raw bytes. Physical/remap tags have separate hash domains. Snapshot consumers
still receive declared labels, not independent authentication of those sources.
The six identity digests cover selected image labels plus the actual supplied
base ADT, whole original736 and whole native1152 bytes. The returned metadata
summary excludes raw paths, names, addresses and bytes; the full inventory is
private caller data.

Independent pre-freeze review reproduced accepted null framebuffer and
outside-DRAM VRAM facts; an alignment check was also missing. The exact source
and reproductions are preserved privately. These target-profile constraints and
regression cases were added before this leaf froze. Two initial mutation-site
selectors were overly broad; their failed harness snapshots are preserved and
were never counted as production failures or semantic mutation kills.

Run `python3 validate.py --output-dir /new/private/path`. Tests use synthetic
boot bytes and include25 compiled assertion-killed faults. The historical audit
can validate only available copied sources and the old80-byte MCC refusal. It
cannot produce a qualified handoff: that capture lacks full384 MCC samples and
separately saved final native BootArgs. No old capture or grade is changed.

No runtime handoff, quiescent calling phase, physical ownership, global census,
policy configuration, target write/readback, packet or hardware boot is admitted
by source validation. The selected worker and final serialization still need
separate integration and review.
