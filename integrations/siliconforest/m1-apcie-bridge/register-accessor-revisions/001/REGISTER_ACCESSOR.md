# Host-only register accessor

`apr_bind_fake` supplies the existing port slices' `apb_io` callbacks from the
existing `test_linkup.c` simulated register file. It is original C and contains
no physical-address dereference, mapping, device access, kernel adapter or pin
operation. The fake callbacks and accessor have caller-owned, single-threaded
lifetimes. This is a tested interface contract, not a sandbox for arbitrary C
callbacks or a hardware ownership mechanism.

Build requires `APB_HOST_TEST_ACCESSOR`. Defining `KERNEL` or the old test suite's
`APB_FAKE_REGISTER_FILE` is a compile error. Thus the linked port slices keep
their PMGR/shared-PHY/pin write paths compiled out. Independently, every shared
region write through the accessor returns `APB_GATED` without a callback.
Only PORT0 and ECAM0 can receive fake writes. Halfword accesses are restricted
to LNKCTL2 at ECAM0+0xa0; a full-word write there is refused, preserving LNKSTA2.
Alignment and subtraction-based end bounds are checked before dispatch. The
slices retain their narrower register/tunable policy. A failed fake read cannot
clobber the caller's output. A failed write remains a failure; the accessor
cannot promise rollback of an arbitrary backend's side effects.

Run `python3 integrations/puredarwin/m1-apcie-bridge/test_register_accessor.py`.
The tests reuse the existing fake file directly, call both original port slices
through the new callback path, exercise local fake register roundtrips and
faults, and require refusal at the existing shared-write gates. The synthetic
completed bring-up record used to reach slice-3 reads is explicitly test data.
It proves no hardware precondition or permission. Six compiled mutants must
fail assertions; the runner also checks compiler refusals, preprocessed shared
branches and real C/C++ linking. ASan and UBSan run with leak detection disabled
for the host environment. No SDK build, kernel collection or target execution
is part of this slice. Existing production sources and frozen receipts remain
unchanged; no new CMake/Nix or live adapter wiring is added.
