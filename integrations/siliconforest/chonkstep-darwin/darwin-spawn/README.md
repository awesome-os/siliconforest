# Checked Darwin managed spawn

This private C boundary uses the pinned Darwin SDK's opaque spawn types. It
does not expose their layout to Rust. The 56-byte request/result layouts are
asserted in darwin_spawn.h. The implementation is source/build preparation,
not yet a runtime-qualified spawn path.

The caller supplies valid owned strings/arrays and keeps its four selected FDs
alive throughout the call. Each selected FD is duplicated with F_DUPFD_CLOEXEC
above fd3. Checked file actions then dup2 exactly those duplicates to child
0/1/2/3. The shim sets CLOEXEC_DEFAULT, signal defaults, signal mask and a new
process group in one assigning setflags call and verifies the readback. It
does not use PATH, a shell, addinherit_np, fork or an alternate spawn path.

Every initialized attribute/action object and every owned duplicate has cleanup
attempted, including after a prior cleanup failure. Close is never retried.
A positive child PID transfers to the caller even when local cleanup fails;
the caller must adopt it when child_created is set, regardless of the return
code. Primary setup/spawn errors and independent cleanup errors are retained.
An ambiguous failed close may leave a duplicate in the parent: it is a recorded
failure, and callers must stop normal launching rather than claim clean state.

Run `python3 test_spawn.py` for the Linux fake-API policy suite: an ASan/UBSan
29-boundary failure sweep, assertion-enabled guard, and six compiled mutants
killed by assertions. These tests never launch a host or target process through
the shim. They establish checked-call/ownership policy, not Darwin inheritance.

Run `python3 build_target.py --output-dir ABSOLUTE_NEW_DIRECTORY` on the healthy
builder for a pinned-SDK ARM64 relocatable object and its exact compile/import
record. This neither fetches inputs nor runs the object. The separate
runtime-probe leaf prepares the actual child FD-whitelist prerequisite. Only a
future bounded M1 test can establish that exec closes unrelated descriptors.

The shim is not a process sandbox. The terminal's separate PTY child still
closes descriptors 3 through its bound before exec. A status-channel message
does not prove parent wait/reap or arbitrary descendant cleanup.
