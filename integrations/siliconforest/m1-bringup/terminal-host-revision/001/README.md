# Terminal host opt-in proposal 001

**Reviewable patch only. Not applied to active source or M5.** Root may apply it
only after the Darwin PTY smoke has a reviewed real runtime PASS and this patch
has independent review. Nothing here authorizes or performs a boot.

`terminal-host.patch` changes four existing files: `host_progress.py`,
`tools/m1_bringup_cycle.py`, and their two test files. `manifest.json` binds each
exact before/after file and the patch. `before/` and `after/` are isolated copies
of the required source closure, used for host tests. `validation/replay/` proves
the patch reconstructs the tested after bytes; it is not an active checkout.

The required base is host `78c258156fc3bb3102b5bb8d674f9bb976a3f5a17ebc2f08f1db008c03dedc70`
and controller `1a6d716c59623a524a573475d528a5aa0e0094b145107db6f40c6180ddfe5056`.
The unchanged companion helpers must also be staged by root:

- TerminalInput: `2b8901e7c29357a3440c40e1db14f5e2416f38beb71af0fee7674cb05233568c`
- VirtualInput base: `689a8fb0fd7728d655d3baefbee18308cb2bce7333f1c0f4a76f37bc8e5673e1`

This adds exactly the following build-record opt-in:

```json
"terminal_input_experiment": {
  "version": 1,
  "origin": "host_pdserialhid",
  "command": "split_nonce_uname_v1",
  "output": [2560, 1600]
}
```

Omission disables it. Null, false, floats masquerading as integers, wrong types,
missing/extra keys and other values fail. It requires the existing exact
`desktop_experiment` and hash-bound RAM root. Presence of either
`virtual_input_experiment` or `keyboard_experiment` conflicts even when null.
The parsed collection must contain PDSerialHID and IOHIDFamily, and exclude
PDM1SPIHID. Actual output must remain 2560x1600, stride 10240, depth low byte 30.
The normal image/receipt/hash/target checks remain in force.

`verify_inputs` runs this validation in the controller's pre-reboot path and
again in the worker. Both scheduler hashes are checked before imports/packet
validation, recorded in `verified-inputs.json`, and copied per attempt before
arming/reboot. The copied bytes are rechecked against both pins too, catching
source drift after initial verification. Missing or changed helper bytes fail
before launching a child.
A failed source-snapshot write cannot proceed to arm or reboot.

The existing ConsoleSession remains the sole reader and writer of secondary
VUART. It selects TerminalInput, VirtualInput, or neither; all boolean defaults
are false. The read loop, exact attempted-byte ledger/fsync/write check, close
ordering and recovery logic are preserved. Observation failure cancels future
typing but keeps draining raw console diagnostics. Short/failed writes stop
the owner, preserve the attempted ledger, and never retry or send a suffix.

Terminal bookkeeping is saved separately as `terminal-input-result.json`.
Existing VirtualInput still writes `virtual-input-result.json`. The console
record adds a `terminal_input_enabled` boolean, not a terminal-success claim.
The controller's existing result is still its independent Darwin console/Linux
recovery verdict; it does not turn a terminal RESPONSE line into a desktop PASS.

The guest command remains the fixed `/usr/bin/darwin-desktop-test '<nonce>'`.
Root must bind a separately reviewed terminal supervisor, terminal binary and
collector into the new RAM image. The `command` opt-in is a fixed schedule
selector, not an arbitrary guest-selected string. The terminal scheduler sends
only its reviewed PRIME/READY-gated, finite split-nonce command. It inherits the
unchanged VirtualInput framing and cancellation code.

Host validation in `after/`:

```
python3 -B integrations/puredarwin/m1-bringup/test_host_progress.py -v
python3 -B -m unittest tests.test_m1_bringup_cycle -v
python3 -B integrations/puredarwin/m1-bringup/test_terminal_input_progress.py -v
python3 -B integrations/puredarwin/m1-bringup/test_virtual_input_progress.py -v
```

These pass 41 + 20 + 11 + 18 tests. New tests cover exact schema/defaults,
null-presence conflicts, malformed types, collection membership, geometry,
pre-backend validation, one-owner selection, actual threaded fake-CDC full
typing/final release, short/failed writes, raw-tail preservation after observer
failure, source drift, snapshots, and snapshot failure before arm. Existing
recovery/deadline/unknown-worker tests remain intact. The real host XKB reference
test ran; these are host-only tests, with no device, SSH, target or SDK action.

Root still must review/apply the patch, refresh the launcher's expected hash and
snapshot binding, stage the unchanged helpers and new packet, and qualify the
actual terminal independently using raw host TX, decoded PTY RX/TX, framebuffer,
cleanup and Linux recovery. A live transcript collector can capture only a
prefix; incomplete output or missing cleanup records must remain incomplete.
