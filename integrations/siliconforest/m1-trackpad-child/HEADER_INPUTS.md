# Root-owned build inputs for the T2 implementation

The adapter will include `child_state.h` and `trackpad_reference.h` from its
build source directory. Astra does not generate or edit these production inputs.

The supplied shared ABI is `struct dl_m1_trackpad_child_state_v1` in root's
`child_state.h`, exactly32 bytes with all offsets checked. It defines the
`DL_M1_TP_*` property/class/length constants used by the adapter.

The private `trackpad_reference.h` defines `dl_m1_tp_descriptor_reference`
with `DL_M1_TP_DESCRIPTOR_BYTES` entries. Its exact generated input and acquired
descriptor digest are bound by root's `descriptor-reference.json`.

The reference is passed only to a full-length equality guard. The child's
descriptor is copied from the successful current-boot enumeration snapshot;
no execution path constructs it from the reference. Host tests may use a
clearly synthetic reference fixture to test comparison policy; that fixture is
not a production header or SDK input.

New adapter sources will be in `adapter/`. They overlay only the isolated build
composition; frozen T1 sources are never edited. Root owns build recipes and
the shared header, and must bind both generated inputs in the SDK manifest.
