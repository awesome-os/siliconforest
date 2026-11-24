# Ordinary terminal network client, revision002 source slice

The new executable `darwin-desktop-net-client` is invoked once by the fixed
interactive shell ENV script. It does not replace or later exec the shell. The
shell waits for this ordinary foreground child and writes its real returned
status, then displays its normal prompt. Forced process-group termination alone
never proves this client's cleanup.

Arguments are exactly `--instance HEX48 --records ABS --seconds N` with the
strict private session/instance ancestry and N10..60. The full session nonce
comes from the checked parent directory basename; an instance alone lacks four
nonce digits. Entry records use actual PID/PPID/session/group/foreground values
and a bounded real descriptor scan before the client opens its own records or
calls the resolver. It never manually closes unknown library descriptors.

The program calls ordinary libc getaddrinfo once for example.com:80, AF_INET,
SOCK_STREAM, TCP and AI_NUMERICSERV. It preserves the complete ordered list,
including duplicates, and uses the first address for one ordinary nonblocking
TCP socket. It records actual getsockname/getpeername endpoints, requests and
reads back TCP_MAXSEG256, writes the fixed186-byte nonce GET, then requires real
recv EOF plus the retained strict HTTP200/text-html/Example-Domain validator.
SHUT_WR follows validated response EOF, matching the qualified one-client TCP
ordering. An observed MSS option is not an assertion about actual wire segment
sizes; the packet evidence must retain the real SYN and retransmissions.

Only a numeric PID file and complete bounded JSON are written inside the
private instance directory. JSON is first written exclusively to a temporary
regular0600 file, data-fsync requested, closed, linked without replacement to
the final name, then its temporary name is removed. Pinned ext4 supports this
linkat path. Its fsync implementation does not provide a proven persistence
guarantee, and these RAM records make no power-loss durability claim. Commit,
directory-close or terminal-output errors prevent actual exit0; an earlier
PASS JSON alone cannot qualify the client. Post-commit/output monotonic checks
also prevent actual successful return after the finite client deadline.

The pure serializer rechecks identities, all successful boolean values, exact
request bytes, global ordered addresses, first-address endpoint, HTTP byte facts
and bounded clocks. It preserves caller outputs on every refusal. Parent and
offline validators must independently check actual closed files, real shell
return/prompt, process lifecycle and raw backend tuples/bytes. No HTTP response
body is printed into the PTY, so it cannot spoof a shell-return marker or prompt.

The production ENV script accepts only the fixed identities and budget, runs
one fixed absolute foreground executable, saves immediate `$?`, reads and
bounds the canonical PID with shell builtins, and prints one exact status line
to its private file and the PTY. No eval, arbitrary command, pipeline, command
substitution, background job, later exec or synthetic PTY input is involved.
The terminal binds exact script bytes and refuses an adjacent .zwc. Existing
app002, frontend007, old captures and libSystem9fc remain unchanged.

Host checks cover43 failed-PASS field cases, every short output buffer, strict
identity/output preservation, ten assertion-killed compiled mutations and an
independent parse of the actual C JSON/HTTP bytes. Five same-version Linux zsh
PTY fixtures exercise the ENV control flow with zero PTY injection and only two
documented literal sandbox path substitutions. They do not execute Darwin's
patched wait path or the native socket client. The real ARM64 build binds all
three translation units, SDK dependencies and the actual9fc/runtime428e import
closure separately from build-time linker libdyld. Build success is not target
execution or desktop/network qualification.
