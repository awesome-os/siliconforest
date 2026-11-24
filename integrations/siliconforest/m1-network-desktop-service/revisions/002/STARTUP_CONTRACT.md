# Desktop startup revision002: fixed ENV and separate ordinary client

This refines immutable001. Whole guest owner180s, desktop90s, close40s,
frontend drain10s, daemon drain5s; host285s. Backend owns its separate420/240/180/
150/40s lifecycle. Source-only until separately built/reviewed/admitted.

New terminal binary `/usr/bin/chonk-desktop-network-terminal` keeps initial
`/bin/sh -i +m`, fd3 CHONKTERM/1 and original pre-exec DS4 checks. Fixed ENV is
`/usr/share/darwin-desktop-network/startup.env`. App compares exact embedded
script bytes, rejects its adjacent `.zwc`, symlinks/writable ancestors, wrong
owner/type/mode/nlink. Script mode0644/root; client executable0755/root. Runtime
new directories are root0700, records root0600/nlink1. Exact records path:
`/private/var/run/darwin-desktop-network/session-HEX48/INSTANCE48`; slot0001/0002,
instance tail equals session first44. Session identity is not secret authority.

Initial environment adds only strictly derived DNDS_SESSION, DNDS_INSTANCE,
DNDS_RECORDS, DNDS_SECONDS (10..60 seconds, at least10 seconds before app limit).
No command/script/URL/IP inputs, ENV override or user config fallback. The fixed
script invokes exactly one foreground absolute native client, saves immediate
`$?`, reads its canonical decimal PID via builtin read, and writes this line to
both its private file and PTY before returning to normal PS1=$ prompt:

`DL30_SHELL_RETURN session=HEX48 instance=HEX48 shell_pid=N client_pid=N status=N\n`

No exec, background, pipeline, eval, substitution or trap. Shell waits/reaps its
normal child; client resolver descriptors never enter the parent shell. Success
requires distinct client PID, PPID/sid/pgid/foreground matching the retained shell,
normal status0, marker/file match, actual later prompt and actual tcgetpgrp match.
No fake raw-grandchild wait status: client_exit_status is shell `$?`. Forced group
kill/direct shell reap alone leaves client cleanup unproven. This separate-process
boundary supersedes the unresolved second-exec idea; no post-libinfo FD sweep.

Fixed added files per instance (no old exporter promotion):

| filename | byte cap | writer |
|---|---:|---|
| network-client.pid | 32 | client, canonical PID followed by LF |
| network-client.json | 16384 | client, complete JSON+LF, private exclusive temp/link commit |
| network-shell-return.txt | 512 | shell, exact marker+LF |
| result.json | 16384 | terminal, existing keys plus startup object |

Client JSON exact scalar/shape contract (integers only, booleans actual JSON,
all keys emitted on success/failure; no nulls/new optional fields):

```
{
 "version":1,"profile":"desktop-native-resolver-two-http-v1",
 "nonce":HEX48,"instance":HEX48,"slot":1|2,
 "pid":N,"ppid":N,"sid":N,"pgid":N,"foreground_pgid":N,
 "status":"PASS"|"FAILED","error":FIXED_TOKEN,"error_errno":N,
 "gai_rc":SIGNED,"system_errno":N,"address_list_complete":BOOL,
 "addresses":[ORDERED_IPV4,...],"selected":IPV4,
 "local":{"address":IPV4,"port":N},"peer":{"address":IPV4,"port":N},
 "entry_fd_check_observed":BOOL,"entry_fd_scan_bound_exclusive":N,
 "tcp_maxseg_request":256,"tcp_maxseg_observed":N,
 "request_hex":LOWERHEX,"response_hex":LOWERHEX,
 "request_bytes":N,"response_bytes":N,"request_shutdown":BOOL,
 "peer_eof":BOOL,"socket_created":BOOL,"socket_closed":BOOL,
 "http":{"status":N,"header_bytes":N,"body_bytes":N,"framing":N,
         "framing_complete":BOOL,"eof_observed":BOOL,"example_content":BOOL},
 "started_ms":N,"elapsed_ms":N
}
```

On PASS: ordinary exactly-one getaddrinfo(example.com,80,AF_INET/SOCK_STREAM/
AI_NUMERICSERV), 1..32 exact ordered nodes including duplicates, globals only,
selected addresses[0], actual local10.0.2.15:ephemeral and peerselected:80, request
exact186 bytes with same full session nonce in both slots, response<=2048, HTTP200
existing immutable validator framing/content plus real recvEOF, socket shutdown
and close. Source port is not preselected. Nonblocking checked socket/select loops
have fixed client deadline; synchronous libc lookup is ultimately enclosed by
finite app lifetime. No retry/fallback. No raw server HTML in PTY: fixed ASCII
BEGIN/DONE summaries only. The native client writes only these new files.

Existing app result keys/schema preserved. Added `.startup` always has exactly:
version1, profile, session_nonce, instance, slot, shell_pid, client_pid,
client_record_complete, client_success, shell_return_observed, client_exit_status,
client_reaped_by_shell, fallback_prompt_observed, foreground_pgid,
foreground_matches_shell, complete, error. Names ending complete/success/observed/
by_shell/matches_shell are booleans. IDs/profile/error strings. Other fields are
integers: client_pid0/foreground_pgid0 and client_exit_status-1 when missing.
No new nulls. `complete` requires all distinct source-bound record/return/prompt/
foreground facts; original cleanup.complete alone cannot promote missing startup.

Client privately commits complete result before normal exit. Shell return file
is closed before final prompt. Terminal captures PTY continuously, recognizes
only exact own-session/instance/shell/client status line, then actual prompt;
network bytes cannot forge either because client never prints them. Final result
cross-checks client JSON/PID/private return record and observed marker/PID/PGID.
Root outer admission must independently read these closed files via its checked
record_guard and require all fields, not trust terminal complete or a C bitmask.
Offline qualifier additionally binds actual wire tuples/raw bytes and source order.

Runtime library is actual libSystem9fc plus retained qualified libdyld428e;
build-time linker b7ce is recorded separately. ENV behavior source refinement is
network-desktop-env-source-001.json0275c131. Original app002/frontend004/coordinator001
and all existing packets/captures remain unchanged. No SSH, hardware, host HID,
GPU observer, route/firewall or existing configuration writes.
