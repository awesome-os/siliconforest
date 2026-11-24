# Shared Wi-Fi/Bluetooth enable: host fake SMC GPIO file only

The separate SMC GPIO 13 control uses ASCII key **gP0d**, not a main AP GPIO
pin-word offset and not PERST GPIO152. The evidence and its board/state limits
are retained in `reports/sprint-010/apcie-perst-power-source-001.json`. This is
a shared radio enable, with potential Bluetooth consequences, not a proven
Wi-Fi-only rail. Its actual level and need for any enable write remain unknown.

At exact local MIT m1n1 pin `121fcbf35d4501814b58ca5347926a4173b6a512`,
`proxyclient/experiments/smc.py:20-29` names pin13 in a Wi-Fi/Bluetooth example.
`proxyclient/experiments/pcie_enable_devices.py:15` writes key gP0d with
0x00800001. `proxyclient/m1n1/fw/smc.py:162-163` packs write32 as little endian,
so the command data is exactly `01 00 80 00`. Key bytes are ASCII, separately
from command endianness. These are source facts, not a transport execution or
J293 electrical specification. No experiment or SMC module is imported or run;
the experiment's unrelated gP1a operation is deliberately absent.

`ars_enable_radio` is original C with one fixed enable command, no off argument,
no caller-selectable key, no read/SMC-start/endpoint/PMGR/PHY implementation,
no delay, no reset pulse and no retry or rollback. Its sole write callback is
compiled only under **ARS_FAKE_GPIO_FILE**. KERNEL plus that flag is a compile
error. Default/KERNEL code returns **ARS_GATED** and invokes no callback at all.
APB_FAKE_REGISTER_FILE is refused, preserving the existing shared-write gates.
No old adapter, source freeze, CMake/Nix, target packet or collection is edited.

The fake backend is a 16-entry, four-byte GPIO command file in host memory.
It accepts only the four-byte gP0d key and four-byte command above, and changes
only fake entry13. It models command acceptance, **not the electrical line**.
`ARS_FAKE_ACCEPTED` and `command_accepted` report that simulated acceptance only;
`state_known` always stays0. Even a fake slot already holding the command does
not justify skipping a request or claiming the real radio is already on. No
unsupported real readback interpretation is invented.

An I/O error may occur after a fake write applied. `dirty` is therefore set
before attempting it, and that partial fact remains on failure; no implicit
disable or restore follows. Callers serialize callbacks and keep context stable.
An arbitrary C callback cannot be sandboxed by this interface. Future hardware
work needs a separate actual SMC owner, board binding, shared Bluetooth policy,
handoff state and settling/ack contract; this fake flag is not such authority.

Run `python3 integrations/puredarwin/m1-apcie-bridge/test_radio_enable.py`.
ASan/UBSan cover fake, default and host-compiled KERNEL modes, exact key/payload,
all sibling slots preserved, unknown-state invariance, repeated explicit enables,
failed writes before/after application and zero-callback default refusal.
Negative compilation, no-callback preprocessor checks, shared-write gates and
seven compiled assertion-killed mutants are included. Leak detection is disabled
for the host environment. No SDK/SMC/DMA/radio/link/boot result is claimed.
No GPL implementation was read or copied for this slice; original project
license selection and upstream provenance remain unchanged.
