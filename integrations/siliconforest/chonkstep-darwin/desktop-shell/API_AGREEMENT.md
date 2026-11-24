# Managed terminal bridge — agreed interface 001

Root confirmed the application contract and text protocol. This file records
the Rust bridge's consumer contract; root still owns the final C ABI/header,
and the separate terminal owner owns the application. No native admission is
implied. Root's SDK attributes remain opaque to Rust.

Executable: absolute reviewed `chonk-terminal-darwin`. Exact arguments:

```
--socket ABS --instance HEX48 --records EXISTING_PRIVATE_0700_DIR --seconds N
```

N is1..120 and fits the session's remaining lifetime minus cleanup reserve.
HEX48 is lowercase, unique for the session. No PATH lookup, arbitrary command,
or identity from a Wayland title/app-id. The private connected Unix stream is
fd3 in the app; manager owns the other side. Root's shim duplicates exactly
four chosen sources to0/1/2/3 with CLOEXEC_DEFAULT and one complete setflags.

Status frames are ASCII, at most256 bytes including LF:

```
CHONKTERM/1 READY HEX48 APP_PID
CHONKTERM/1 CLEANUP HEX48 APP_PID COMPLETE RAW_WAIT_STATUS TERM_SENT KILL_SENT
```

The lines above include a final LF on the wire. Fields use single spaces,
canonical decimal numbers, and exact0/1 flags. RAW_WAIT_STATUS may be-1 only
when COMPLETE=0. COMPLETE=1 requires an actual terminal PTY wait status; the
application PID's wait/reap remains independently owned by the manager.
READY is optional before CLEANUP, but each can occur once only; READY after
CLEANUP, duplicate/bad/truncated/oversize/foreign-ID-or-PID frames fail.
Partial reads are accumulated within the same256-byte bound. EOF alone is
never a cleanup acknowledgement. Missing/invalid status remains a sticky
failure even if waitpid later reaps the application.

The manager retains returned PID ownership if the shim reports child_created
plus local cleanup errors. It must not turn that outcome into an ordinary
error that loses the live child, retry the spawn, or signal an inferred PID.
All lifecycle transitions are serialized in the compositor event loop.

Launch readiness is exec/keymap/SSD/actual-frame readiness, independent of
focus and held keys. Accepted input stays queued to the same PTY across leave;
held arrays produce no characters. The app owns PTY cleanup and fd3 closure;
its shell closes status/Wayland/log FDs before exec.

Implementation admission remains blocked until root's exact header and C
leaf and the separately frozen app are bound. The bridge's default production
spawner returns an explicit Unsupported result. Host fakes exercise ownership
and protocol behavior only and cannot satisfy native readiness.
