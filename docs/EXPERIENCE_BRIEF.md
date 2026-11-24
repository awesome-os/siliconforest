# Experience brief, proposed

## Goal

An Apple Silicon-first Unix desktop with Chonkstep's own interaction model, coherent gestures and scaling, transparent developer tooling, and a recovery story users can understand. The product should feel intentionally designed rather than like unrelated desktop components sharing a theme.

## Design work possible without a running kernel

Describe how a user launches, locates, moves, tiles, switches and closes windows; how workspace overview responds to fingers; how keyboard access matches pointer actions; how fractional/high-density scaling should look; how unavailable hardware is represented; and how settings persist. Write interaction stories and acceptance criteria before making a mockup look like a working implementation.

Prototype the shell's behavior with explicit fake outputs/input rather than pretending it has native GPU or display support. Keep that prototype separate from actual Darwin integration status.

## Application acceptance ladder

1. Shell, files and terminal: process spawn/exit, signals, pseudoterminal resizing, UTF-8, permissions and paths.
2. Developer tools: Git, compiler/toolchain, Python/Rust where ported; reproducible dependency setup.
3. Networking: DNS, clock, TLS chain validation, certificate store, failure handling and credential storage.
4. A specific open-source terminal application compiled from pinned source, with its dependency surface recorded.
5. Named AI clients, tested individually for sign-in, key handling, subprocesses, streaming output and cancellation.
6. A browser or native GUI toolkit only after identifying its actual renderer, process and security requirements.

These are proposed tests, not statements that current macOS binaries will run. Do not treat HTTP API access, a working browser, a CLI port, and a proprietary desktop app as interchangeable compatibility milestones. Do not use valuable credentials on the prototype before the trust and isolation story is reviewed.

## Avoid premature commitments

No blanket promise of macOS app compatibility, Linux binary compatibility, production-quality sleep, Metal, browser support or security parity with macOS. No product name, release date, or percentage-complete estimate is required to start useful engineering.
