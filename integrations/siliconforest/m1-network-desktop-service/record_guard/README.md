# Closed-record reader and JSON queries

Original read-only module for the future desktop network owner. It does not
launch, signal, write, delete or qualify any child. The caller first establishes
that the relevant record writers have exited/closed their files. Metadata
stability is an additional check; it cannot establish writer quiescence itself.

`rg_read(session, nonce, slot, basename, scratch, &snapshot)` accepts exactly
`/private/var/run/darwin-desktop-network/session-HEX48`, with matching lowercase
48-byte nonce. Slot0 reads that directory. Slots1/2 derive the only permitted
instance names: `0001`/`0002` plus the nonce's first44 characters. Basenames are
1..64 lowercase ASCII letters, digits, hyphen, underscore or dot, with no leading
dot; slash and other characters fail. The parent must use its own fixed admitted
record-name list, not a record-provided filename.

Every component is opened relative to the retained parent FD with NOFOLLOW,
NONBLOCK, DIRECTORY and CLOEXEC. Production starts only at `/` with owner UID0.
System ancestors `/private`, `/private/var`, `/private/var/run` must be root-owned
directories without group/other write; the application base, session and instance
must be exactly0700/root-owned. All directory FDs remain open through validation.
Files are opened NOFOLLOW/NONBLOCK/CLOEXEC, must be regular, exactly0600/root-owned,
have one link and size0..256KiB. Raw `.txt`/`.bin` snapshots are supported too.
Native-terminal fixed caps are enforced additionally: result.json and
network-client.json16KiB, network-client.pid32 bytes, network-shell-return.txt512
bytes. The latter two are raw decimal-PID and DL30_SHELL_RETURN records; the
parent separately validates their exact grammar and bound identities.

The reader checks initial fstat, reads at most the declared size in4KiB chunks
with at most128 read-loop calls, then requires EOF. It rechecks dev/ino/size,
mode/owner/link count and nanosecond mtime **and ctime**, checks the pathname still
binds the same regular file, and rechecks every acquired directory's identity and
mode/owner through its parent. Directory/file dev+ino and file timestamps are
published in `rg_snapshot`; compare directory identities across related reads.
This is a point-in-time observation, not a long-lived pathname capability.

All owned FDs get exactly one close attempt. I/O errors, EINTR, short EOF,
unexpected extra bytes or close uncertainty refuse success; no retry can repair
an uncertain snapshot. If close itself fails, the module cannot promise that FD
is gone. NONBLOCK and finite loops do not guarantee bounded filesystem syscall
latency. No ACL/xattr or hostile privileged-writer protection is claimed.

Allocate `rg_read_workspace` and `rg_json_workspace` on the caller's heap/static
storage, **not the stack**. They are scratch. Successful snapshots/documents
borrow their buffers, which must remain unchanged and serialized until all
queries finish. Use separate storage or finish extracting scalar facts before
reusing scratch. On failure, output structs/scalars/string buffers remain
unchanged; scratch contents are unspecified. Do not interpret an old borrowed
document after a failed parse has reused its token workspace.

`rg_json_parse` implements the approved conservative record dialect:

- At most256KiB input,4096 tokens,32 value nesting levels and4,194,304 parsing/key
  comparison steps. All limits refuse rather than truncate. Whitespace is JSON's
  four ASCII whitespace characters; no BOM, comments or trailing bytes.
- Objects, arrays, strings, booleans, null, int64-negative and uint64-positive
  integer literals. Fractions, exponents, NaN, Infinity and overflow fail.
- Raw and decoded string characters must be ASCII. Standard escapes and ASCII
  `\uXXXX` are accepted. Non-ASCII/surrogate escapes and raw control bytes fail.
  Escaped NUL/control values retain their byte length; query callers must compare
  returned length and bytes, not assume `strlen` describes a JSON string.
- Unknown object keys are allowed. Every object's duplicate **decoded** keys
  fail, including escaped aliases, empty keys and duplicates nested in unknown
  fields. There is no substring or textual-key matching shortcut.

Use `rg_json_path` with an explicit array of `RG_KEY` (key pointer+length) or
`RG_INDEX` steps; no dotted-path ambiguity. An empty path selects the root.
Typed `u64`, `i64`, `bool`, `null` and `string` getters reject wrong types and
range loss. `u64` rejects negative literals including `-0`. String capacity must
include an additional NUL terminator; embedded JSON NUL still counts in length.
Input/output overlap is refused. Parsed bytes/token metadata are trusted
unchanged caller storage, not an arbitrary-memory capability boundary.

Typical parent sequence: after actual app writer cleanup, read fixed result.json,
parse, query/retain required typed fields plus acquired directory identity; then
read fixed client record into another scratch or after finishing those queries,
compare directory identity and independently match the client's PID/session/
instance/network fields. A valid JSON document or stable file alone proves none
of those semantics. The parent still verifies expected complete schemas, raw
records, process outcomes and packet evidence before retiring the daemon.

`python3 -B check.py` runs actual host filesystem fixtures with a compile-time
test root and current UID, sanitizer/NDEBUG tests, a separate Python JSON oracle,
compiled mutation checks, static analysis and real pinned ARM64 SDK compilation.
Production objects contain no alternate-root/UID/race hooks. File tests cover
symlink/hardlink/FIFO/directory/mode/size refusals, directory rebinding and
same-size rewrites whose mtime was restored; ctime still exposes that fixture.
Mutation checks reject attempts to remove NOFOLLOW before any target file read.
Host tests do not establish Darwin filesystem/process behavior or GPU progress.

The surrounding frozen proposal, ENV report, original app/frontend and all GPU
leaves are unchanged. GPU observer HOLD remains in force.
