# PERST GPIO slice: fake file only

The fixed subject is J293/T8103 APCIE port 0's external **main AP GPIO 152**,
pin-word offset `152 * 4 = 0x260`. The independently retained saved ADTs resolve
`function-perst` through main GPIO phandle 119, with arguments 152 and 0;
`reports/sprint-010/apcie-perst-power-source-001.json` records both exact blobs.
This is distinct from CLKREQ GPIO 150, the internal APCIE reset register and
the shared Wi-Fi/Bluetooth SMC enable. No SMC operation is implemented here.

`apg_set_perst` is an original bounded C model. Only an explicit
`APG_FAKE_GPIO_FILE` host build contains GPIO callback calls. Default builds,
including `KERNEL`, return `APG_GATED` before any GPIO read or write. A combined
KERNEL/fake build is a compiler error. The old `APB_FAKE_REGISTER_FILE` opt-in
is also refused, keeping shared PHY/PMGR writer paths compiled out even in
the GPIO tests. Nothing is added to CMake, Nix, an IOKit adapter or a collection.

The fake-only operation models active-low PERST#: assertion clears DATA bit 0;
release sets it. It first refuses LOCK bit 21, any nonzero PERIPH bits 6:5 or
a MODE bits 3:1 value other than 1. It requires output configuration to exist;
it never changes mux, direction, group, pull, drive or input-enable settings.
Only DATA changes in the one read-modify-write. A single readback checks DATA,
LOCK, PERIPH and MODE, preserving other observed fields without asserting their
coherence. Failures do not trigger retries, restoration or a reset pulse.
The caller serializes access; this provides no concurrent hardware ownership.

The MIT source at exact m1n1 pin `121fcbf35d4501814b58ca5347926a4173b6a512`,
`proxyclient/m1n1/trace/gpio.py:6-21`, describes these field positions and the
212-pin, four-byte stride. Its speaker-amp experiment at lines 103-105 shows a
different reset output using MODE=1 and DATA low/high. That supports the model's
field interpretation; it does not establish J293 GPIO152's live mux, ownership,
lock state, electrical polarity/inversion, power, timing or readback behavior.
No Python transport/tracer/experiment is imported or executed. No upstream
implementation is copied, and no GPL source is used for this slice. Original
project license selection remains pending; upstream provenance is unchanged.

`apg_snapshot` describes one call, not coherent hardware state. Read values
have explicit validity flags. `dirty` is set **before** attempting a write,
because a failing callback may have applied it already. An I/O failure remains
a failure, with no rollback or success claim. There is no hold-time or link
authorization implicit in a fake assert/release pair.

Run `python3 integrations/puredarwin/m1-apcie-bridge/test_perst_gpio.py`.
ASan/UBSan test fake, default and host-compiled KERNEL modes. The fake GPIO file
has 212 words, refuses every offset except 0x260, and checks all sibling words
unchanged. Tests cover assertion/release, all wrong mux/mode combinations,
LOCK, both read faults, write failures before/after application and checked-field
readback mismatches. Negative compilation, preprocessed no-callback gates and
seven compiled assertion-killed mutants are included. These are host checks,
not an XNU SDK build, GPIO hardware test, real link bring-up or boot clearance.
