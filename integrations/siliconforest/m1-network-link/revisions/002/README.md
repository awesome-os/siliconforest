# Controlled link revision002

Preserves the failed001 sources/packet/capture. The only guest behavior change
is a five-descriptor select adapter instead of poll. The pinned ARM console
has cnselect/ttselect but no kqueue opt-in; poll returns POLLNVAL and the old
loop ignored it, leaving READY and OPEN queued. No syscall revents were logged
in001; this is source-grounded diagnosis matching the captured failure.

The adapter checks FD_SETSIZE, clears sets/revents, resets20ms timeout, preserves
partial writes, EOF, SO_ERROR and EINTR handling. All transport, backend, profile,
console SPACE/LF alphabet, budgets, kernel, input, libSystem and recovery sources
remain byte-identical. Host select tests exercise the actual extracted function;
268 parser cases/8033 assertions and3 readiness/bound mutations also pass.
SDK build uses the same pinned toolchain/canonical libSystem; select replaces
poll as the sole changed import. Runtime qualification remains required.
