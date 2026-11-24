# Darwin IPC platform candidate

This isolated patch lets the actual chonk-ipc and shared chonk-shell sources
type-check for aarch64-apple-darwin. It is not integrated into a shipped
compositor, linked desktop session, or guest image. No Darwin runtime is claimed.
The qualified717da106 source and every existing packet remain unchanged.

Linux keeps its socket/accept4 flags and SO_PEERCRED path. Darwin supplies its
sockaddr_un length, creates sockets with socket/accept, then sets and reads back
O_NONBLOCK and FD_CLOEXEC before returning an owned fd. Any error closes it.
A live socket's EINPROGRESS or any error other than ECONNREFUSED cannot authorize
unlinking its path on Darwin. Six host tests exercise ownership and flag failure
paths on Linux; they are not Darwin syscall execution.

Darwin UID/GID come from the real getpeereid connection credential snapshot.
PID comes from SOL_LOCAL/LOCAL_PEERPID with checked result, exact length and
positive value. XNU returns the peer socket's last_pid, which can change when
the socket is used by a different process. This is explicitly different from
Linux's connection-time creator PID; PID is not an authentication authority.
Both existing UID admission policies remain unchanged. A failed credential
query returns an error, never the current process's UID or an invented PID.

The socket-to-fcntl interval is not atomic with concurrent process creation.
This remains an integration gate, not an accepted race. The pinned Rust Command
probe under runs/m1-desktop-010/darwin-spawn-proof-001 has no observed0x4000
CLOEXEC_DEFAULT flag on its ordinary spawn path. An independently reviewed
explicit Darwin spawn implementation or complete spawn/IPC serialization is
required before these IPC calls coexist with desktop background spawning.
The actual shared shell currently has background Command users. Do not simply
combine this patch with the shell and call the gate complete.

Primary sources are the pinned XNU sys/un.h and uipc_usrreq.c, the pinned libc
getpeereid.c, canonical16K libSystem, and the locked libc0.2.189 Apple definitions.
The separate contract review is
reports/sprint-010/chonk-ipc-darwin-port-contract-review.json. Source/SDK/build
evidence is separate from future runtime peer identity and process cleanup.
