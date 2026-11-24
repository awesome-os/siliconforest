# Managed-spawn runtime whitelist prerequisite

Original Darwin parent/child source fixture using the real, separately frozen
`chonk_darwin_spawn_v1`. Exact runtime criteria, deadlines and private record
format were fixed before execution in [CONTRACT.md](CONTRACT.md).

Root installs these names with the pinned SDK and canonical import closure:

```
/usr/bin/darwin-spawn-whitelist-parent
/usr/bin/darwin-spawn-whitelist-child
```

Parent accepts only `--nonce LOWERCASE_HEX48`, creates its own unique private
directory, and launches only the absolute fixed child via the real adapter.
The producer must preserve the parent header include `../darwin_spawn.h` and
link `../darwin_spawn.c`; neither file is owned or copied by this fixture.
There is no C test shim, host spawn, Rust Command or fallback executable.

The actual child inventories the real soft-limit range, checks all four
selected descriptors and performs the stdin/stdout/stderr/fd3 nonce exchange.
Opened and SCM_RIGHTS-received ambient descriptors deliberately remain without
CLOEXEC through spawn. Nonblocking/CLOEXEC setters have checked readbacks;
SIGPIPE is explicitly ignored and checked in both processes. Deadlines and
overflow detection bound partial/broken/saturated endpoint behavior. Parent
owns positive child PID/wait state even after a local adapter cleanup error,
and never signals after terminal wait or loss of ownership.

Host validation only:

```
python3 test_probe.py
```

These ten tests use synthetic record/classifier examples and source policies,
including unsafe-source negative controls. They do not compile C, execute the
fixture, substitute Linux inheritance for Darwin or prove loop behavior at
runtime. Real API behavior, supported FD limits, timed failure handling and
the actual FD whitelist remain pending root's build and target execution.

After root retrieves the four closed records and actual parent exit status,
`qualify.py --records PATH --parent-exit-code ACTUAL_CODE` checks them strictly.
Its success means the supplied records meet their criteria; execution/artifact
provenance must be bound separately. A summary/footer alone cannot qualify.
Maximum result JSON is 12,287 bytes; each raw transcript is capped at 256 bytes.
No serial exporter, packet, supervisor, build producer or hardware access is
included here. DS4's PTY-shell descriptor test remains in the separately frozen
terminal application and is not replaced by this direct-spawn child fixture.
