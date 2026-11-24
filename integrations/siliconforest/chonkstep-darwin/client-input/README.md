# Host-origin virtual-input observer

`darwin-wayland-client-input --socket ABSOLUTE_PATH --nonce HEX48 --seconds N`
is a separate copy of the frozen SSD client. `N` is 1–120. Both earlier client
projects, binaries, the compositor and host/controller are unchanged. This
client **observes real Wayland callbacks**; it injects no events, opens no HID
queue or serial device, and provides no physical-keyboard, trackpad or GPU proof.

The existing compositor must later be launched with `--input sfhid`, after both
real SFSerialHID-backed character nodes have been primed. Root owns that future
launcher and fixed VUART sequence. The design is recorded separately in
`reports/sprint-010/desktop-virtual-input-plan.json`; this project does not enact
it. No fallback input or fabricated seat/event is supplied when those steps are
missing.

The original SSD negotiation, 800x480 nonce/color bars/moving marker, unlinked
SHM backing file, release discipline and real frame callbacks remain intact.
In addition, readiness requires:

- Exactly keyboard + pointer capabilities on a single advertised `wl_seat`,
  version at least 5 (for pointer frames), bound at most version 9.
- A real `wl_keyboard.keymap` event in XKB v1 format, regular-file descriptor,
  declared size 2..1 MiB, readable bytes, final NUL, UTF-8 and the four XKB text
  sections. This is **text-shape checking, not semantic XKB compilation**.
  Byte count and FNV-1a64 are logged as diagnostic identity, not a security hash.
- Keyboard enter on this client's surface with no held keys, and an observed
  all-zero depressed/latched/locked modifier state and group.
- Applied/acknowledged server-side decoration and a real frame callback.

Only then does `DL10_INPUT_READY NONCE origin=host_sfserialhid ...` appear.
The observer allows 15 seconds from readiness for this exact sequence:

1. `wl_keyboard.key` evdev **30** pressed, then released, on the focused surface
   with neutral modifiers. This is the evdev A key; no decoded text claim is made.
2. Pointer enter on this surface at local **(200,100)**, within one pixel per
   axis. Optional motions before the click must stay within that same tolerance.
3. One BTN_LEFT (**272 / 0x110**) press, then release, at that point, followed
   by `wl_pointer.frame`.

Actual key/button serials and timestamps and actual pointer coordinates are
logged with the nonce. `DL10_INPUT_OBSERVED ... origin=host_sfserialhid` is emitted
once only after all conditions hold. Host-origin is the experiment label: the
client cannot distinguish the kernel's physical and serial event sources. Root
must independently bind the exact host TX transcript, candidate, phase order
and capture; a marker alone cannot establish provenance.

Unknown capabilities, wrong focus, held keys/modifiers, unexpected/repeated keys
or clicks, wrong coordinates, scroll or missed deadline are sticky failures.
An eventual titlebar drag may produce pointer leave after the content click;
it must produce no additional client button events. This client cannot report
global window position: displacement needs an independently captured framebuffer.
No titlebar movement is performed here. Later unexpected events still fail even
if an earlier observation marker was printed; preserve the full final record.

At the outer deadline, success requires the complete input proof and at least
one real frame callback; otherwise exit is nonzero. Normal teardown destroys
the decoration before its toplevel. Closing this client does **not** release the
compositor's SFHID console grab or SFGOP ownership. Their orderly shutdown and
Linux recovery remain the owner's separate obligations.

The inherited initial registry roundtrip is blocking. The 15-second observation
deadline and 1–120-second main loop do not replace the independent host-worker
timeout. No human interaction or typing is requested.

Offline validation:

```sh
cargo test --offline --locked --target-dir target
python3 -B test_source_contract.py
```

Tests exercise pure mode/input state and source wiring; they are not a live
Wayland, native serial, native input or Darwin runtime result. Root cross-builds
the distinct binary later using the pinned SiliconForest wrapper.
