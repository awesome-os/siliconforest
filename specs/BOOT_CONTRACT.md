# Boot and control contract, draft 0

This is a specification to implement, not an XNU loader or a valid boot profile.

## Target inventory

Record model, board ID, SoC ID, CPU topology, RAM, firmware versions/hashes, existing boot arrangement, interrupt-controller generation, intended kernel page granule, selected console channels, and a separately tested recovery procedure. Do not infer these from the M5 host model.

## Payload record

Every experiment must identify source commits and dirty-tree status; build inputs and flags; Mach-O/kernel-collection format; architecture; kernel and symbol hashes; initramfs/root image hash; loader hash; m1n1 host and target revisions; firmware dependencies; and boot arguments. Proprietary artifact hashes may be recorded without redistributing their bytes.

## Handoff questions to answer from exact sources

Kernel entry format; boot argument version/size; Apple device tree contents and ownership; physical/virtual memory map; firmware-reserved pages; MMU/exception state; CPU bring-up state; interrupt/timer routing; and memory retained by firmware services. Never obtain these values by guessing from Linux defaults.

## Future controller flow

PRECHECK -> APPROVAL -> ARTIFACT_VERIFICATION -> CONTROL_BASELINE -> LOAD -> OBSERVE -> COLLECT -> STOP.

A separate deliberate action handles recovery. The controller owns the proxy connection; a collector must not independently open that same stream. Route proxy control and any virtual UART capture explicitly after confirming how the selected m1n1 revision exposes them.

A failure or timeout must preserve raw output, error state, elapsed time, source profile and recovery outcome. Do not automatically run reset/DFU/partition/boot-policy commands.

## Proposed milestone event format

The fixture parser consumes one JSON object per line with `schema_version`, `run_id`, `marker`, and `origin`. Proposed marker order is KERNEL_ENTRY, EARLY_CONSOLE, VM_READY, INIT_READY. `origin` is `synthetic` in fixtures and `capture` in unverified captured-event files.

No existing kernel instrumentation is claimed to emit these events. A future adapter must preserve raw byte streams separately and bind a report to the launched artifact and target identity. A run ID prevents accidental mixing, not malicious spoofing. A line saying INIT_READY is not independent evidence of a working shell.

The parser recognizes structured PANIC/EXCEPTION observations for the selected run and conservative raw panic lines. It never promotes observations to hardware PASS. Missing, malformed, out-of-order, or unrelated events remain warnings or failures, not fabricated progress.

## Safety boundaries

Early target design: RAM-root and deliberate exclusion of internal-storage writes. That is a goal requiring controller/driver enforcement, not an isolation guarantee from the hypervisor. A faulty DMA-capable driver can violate assumptions. Do not expose important storage or secrets merely because a VM label appears in the command.

Reference: Asahi's m1n1 hypervisor documentation requires matching host and target ABI revisions. See `docs/SOURCES.md`.
