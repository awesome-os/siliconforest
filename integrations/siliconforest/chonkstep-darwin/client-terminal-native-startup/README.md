# Native keyboard startup observer

This separate finite client runs the real guest `/bin/sh` PTY and renders actual output. It observes PTY/keymap/own-focus/SSD/completed-frame readiness without requiring a person to type. It never inserts commands or reports input success. `DL12_NATIVE_STARTUP_READY` and `DL12_NATIVE_STARTUP_DONE` explicitly say `physical_input_proven=false`; exit 0 requires readiness and complete owned-child reap, and is only a startup result.

CLI: `darwin-wayland-terminal-native-startup --socket ABSOLUTE_PATH --nonce HEX48 --seconds N`. The original qualified host terminal and all its source hashes remain unchanged. Shared source modules are exact copies of its revision 2. Its strict response recognizer remains diagnostic only; a reply cannot be fabricated by this client. Actual Wayland keys are still the only PTY input.

Use only with a separately reviewed native-keyboard desktop packet, real descriptor-backed endpoint publication, a keyboard-only compositor seat, no host HID schedule, and a TX ledger that ends at the fixed launcher command. Readiness, endpoints and enumeration are not physical typing proof. No new firmware, kernel control, device open, pointer capability or input injection is added here.

Root cross-build, source review, RAM readbacks, owned bounded runtime and Linux recovery remain required. Host tests do not establish Darwin or physical input. All dependency versions and font bytes are retained from the original locked client.
