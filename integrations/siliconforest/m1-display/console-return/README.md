# IOGOP text return after server close

This source candidate addresses the missing text-console return in desktop
attempt003. The saved run reached `IOFramebuffer::close()` and both processes
exited zero, but the framebuffer retained the last graphics frame. The generic
close's `getPowerState()` guard is incompatible with IOGOP's unregistered PM
table, whose state stays zero. The exact close-time state was not captured.

The new override calls `super::close()` first, under the caller's existing system
and framebuffer workloop gates. It then requests `kPEAcquireScreen` only when:

- a server existed before parent cleanup and neither server, shared connection
  nor captured state remains afterward;
- the active platform provider passes the existing AppleARM firmware predicate;
- the instance and provider are attached/active, and the instance is neither dead
  nor suspended;
- PM state is zero, leaving the ordinary powered-console path alone;
- the current text console still describes this retained firmware base, width,
  height and stride, with 30- or 32-bit pixels;
- `pdtextboot` is enabled and `serial_video_mirror` or `gopconsole` requests video.

It does not create a power-state table, set a PM state, clear pixels directly,
change the console mode or modify generic IOGraphics code. Kernel003's existing
Acquire implementation restores the original console route. A new server still
releases ownership through the unchanged parent open path.

Apply **one** of these routes to exact PureDarwin
`e7aa00a60f0b56ef35aa24252e81467787bf71ea`:

1. Apply the frozen `../framebuffer-30bit.patch`, then
   `../../m1-wayland/iogop-vram-write-combine.patch`, then `console-return.patch`.
2. Apply `console-return-cumulative.patch` directly to the pin. It includes those
   two framebuffer prerequisites plus this delta, with identical resulting bytes.

Both routes change only `IOGOPFramebuffer.cpp` and `IOGOPFramebuffer.h`. They
require the separately preserved native kernel003 console-resume behavior; the
cumulative patch does **not** contain any kernel changes. Do not apply it on top
of the already layered framebuffer source. `source-validation.json` binds input,
intermediate and result hashes and the owned source set. Root owns
`build-framebuffer.nix` and `build-kc.nix`; those recipes are outside this freeze.

The one-line diagnostic contains only a numeric refusal mask or the actual
Acquire-request return code. Mask bits are:

| Bit | Refusal |
| --- | --- |
| `0x01` | No server before parent close |
| `0x02` | Server/shared/captured state remains |
| `0x04` | Inactive, dead or suspended |
| `0x08` | Nonzero PM state; use ordinary parent behavior |
| `0x10` | Platform/console description unavailable |
| `0x20` | Different or invalid framebuffer geometry/format |
| `0x40` | Console is not firmware text mode |
| `0x80` | Text boot or video routing opted out |

An `acquire-request` line records a reached API call, not visible text or panel
success. Non-Apple/non-platform providers take only the unchanged parent path
and emit no new diagnostic. A surviving shared client suppresses the fallback;
this patch intentionally adds no last-shared-client callback or retry.

Run the focused, offline host test:

```sh
python3 integrations/puredarwin/m1-display/console-return/test_console_return.py -v
```

It reads exact pinned files with `git show`, applies patches only in temporary
directories, and executes the extracted new C++ close body and firmware predicate
with fake registry/PM/IOKit objects. It links the actual kernel003 ARM console
control branches extracted by the existing console-resume test, with fake pixel
sinks. The 41 scenarios cover positive, negative, tagged-address, failed API and
reopen paths. The original parent-only path reproduces missing return; 11 runtime
mutants test the guards and Acquire operation. The tests do not establish SDK
compatibility, actual locking, RPC quiescence, mapping revocation or scanout.

The scoped candidate uses the reviewed single-owner PDGOP presenter: rendering
ends before it requests unmap and closes its connection. Closing a connection is
not general proof that every duplicated mapping has been revoked. Legacy raw
`/dev/fb0` access has no ownership arbitration and is outside this experiment.
Neither mapping quarantine nor any existing lifetime rule is changed.

Root must build and review the resulting bundle/KC, then bind any authorized
runtime to those exact bytes. Required runtime evidence is the real close and
Acquire-request sequence followed by a fresh nonce footer **in framebuffer
pixels**, plus verified Linux recovery. Serial footer and process exit alone
remain insufficient. Physical-panel observation is separate.
