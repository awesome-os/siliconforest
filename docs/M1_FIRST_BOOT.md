# First M1 experiment: XNU entry and console

The selected target is the user's 13-inch M1 MacBook Pro. Authorized read-only
SSH inventory observes the expected Linux DT model and `apple,j293` /
`apple,t8103` compatibles. It currently boots Arch Linux ARM through an existing
m1n1/U-Boot chain. No target reboot, installation, serial connection or boot
policy change has been performed by this increment.

The proposed first experiment reaches our compiled XNU entry, emits one line,
and parks before VM initialization. It cannot reach a Darwin shell. The
unmodified ARM64 virt baseline and experimental RAM probe already have separate
VM evidence; neither establishes the Apple platform port.

## Candidate and source review

The exact PureDarwin source pin remains
`e7aa00a60f0b56ef35aa24252e81467787bf71ea`. The isolated candidate checkout is
`sources/puredarwin-m1-firstboot`. The source patch, exact-file manifest, focused
tests and entry contract are under `integrations/puredarwin/m1-firstboot/`.
`CONTRACT.md` defines the selected native T8103 geometry and deliberately
unreachable normal kernel path. `BUILD_DIAGNOSIS.md` records missing private
interfaces and the restricted public-source configuration; no successful
validation stubs stand in for them. Actual build results are recorded in
`reports/sprint-007/kernel-build-*.json`. Attempt 006 succeeded; its linked
entry, permanent park and four explicit unsupported-operation failure bindings
passed artifact inspection. This is a diagnostic link, not a complete kernel.

The payload preserves x0, needs no stack, checks EL1/MMU-off/Apple CPU identity,
uses bounded polled UART accesses, and parks with interrupts masked. The only
checkpoint line is:

```text
HVLOG: DARWIN_LAB_M1_EARLY_CONSOLE checkpoint=entry-before-vm next=park kernel_init=not_started
```

That line is an entry/console observation only. The normal Apple CPU/interrupt
path still has a GIC dependency to replace. Full native initialization, security
backends, storage, GPU, display and userspace are outside this first payload.

## Controller preparation

Read-only SSH confirmed the M5 controller and its macvdmtool installation.
An isolated user workspace now exists at
`/Users/chriskearney/darwin-lab-firstboot-007`. It contains the exact pinned m1n1
host sources and a Python 3.14 environment installed offline from hash-locked
Construct 2.10.70 and pyserial 3.5 wheels. Proxy module imports succeed without
opening a device. No system Python or existing maclab installation was changed.

The new debug/proxy m1n1 binary is built from
`121fcbf35d4501814b58ca5347926a4173b6a512`, with that complete commit as its
`BUILD_TAG`. The installed target reports stage1 `v1.6.1-dirty` and stage2
`f5a82fdb-dirty`; those are not a match. Package metadata does not prove the
live proxy ABI. The prospective hardware session must establish the matching
debug proxy before the new client can launch this guest.

No proprietary firmware is copied into the host packet. The pinned m1n1 loader
copies the target's existing SEPFW and TrustCache inside target RAM when loading
a guest; their presence and ranges are handoff preconditions, not permission to
extract or distribute them.

## Review packet

`tools/m1_firstboot.py` checks the plain ARM64 DEBUG T8103 Mach-O, bounded load
commands, executable entry, `__start`'s actual branch to the diagnostic symbol,
and its instruction prefix. It binds the packet to the reviewed patch and
matching kernel/m1n1 build receipts. Hash mismatches, wrong-board VM kernels,
changed entry branches and existing output directories are refused. This
stdlib-only tool creates review copies; it never opens a device or authorizes
execution. Receipts remain build observations, not cryptographic attestation.

The real packet is now at
`/Users/chriskearney/darwin-lab-firstboot-007/packet-r6` on the M5.
The inspector passed there, and the pinned parser prepared identical image
bytes on Linux and M5. See `reports/sprint-007/m5-packet-verification.json`.
Loading image bytes into a Python bytearray does not launch a guest. Preserve
the prepared hash and entry offset for comparison with the physical session.

## Physical handoff conditions

Before an operator-approved physical attempt, establish the actual proxy
transport, a working recovery procedure, and the matching debug image/client.
Then verify the live target class, chip, exactly one running boot CPU, UART
translation, boot-args revision and required memory-map ranges. Guest MIDR is
virtualized through VPIDR_EL2, and the loader does not establish its value or
SCTLR_EL1 for us; inspect these before entry. The preflight component must not
turn a JSON assertion into a hardware attestation or execution permission.

The pinned `run_guest.py` is a reference, not an inventory command: it initializes
the hypervisor, maps real MMIO, copies firmware regions in RAM, resets a PMU
panic counter, and eventually enters the guest. A first-console launcher should
omit the unrelated PMU reset and preserve a single owner of the proxy stream.
The pinned hypervisor is not storage isolation; removing ADT nodes is not a DMA
barrier. Our parked entry never reaches a storage driver.

The installed macvdmtool also has side effects before its command dispatch:
even `nop` can enter and leave DBMa. Its source was read and its binary hashed;
it was not executed. Cable mode and correct ports must be checked deliberately.
Its serial mode requires a SuperSpeed-capable cable; debug USB has different
transport requirements. A listed `/dev/cu.debug-console` alone proves no link.

## Observation and stop

Capture the full host/proxy stream. In this hypervisor route, the UART is
emulated: status reads report TX-ready/empty, so a successful line does not
validate the physical UART, polling timeout or final drain. The `HVLOG:` prefix
allows the line through m1n1's own console. Do not run a competing serial reader
against the control connection.

The guest parks instead of returning. The bounded launcher attempts one timed
interrupt at a guarded protocol boundary, saves the original exception context
and compares its PC/x19/x0/CPU/DAIF/mode with the loaded image contract. Ordinary
MMIO service is allowed before capture; no guest continuation is sent afterward.
The full console line and context require independent review. No automatic
hardware PASS is produced. Hypervisor preemption with guest interrupts masked
remains a hardware unknown, and its watchdog is off by default. A missing
capture or timeout is inconclusive. If control cannot be regained, use the
separately verified recovery procedure; there are no unattended reboot/DFU loops.

The launcher sends no reset or resume command when it stops, but that is not a
guarantee of target state. In the pinned source, a transport read failure during
a nested exception proxy can return -1; the hypervisor then defaults to
`flush_and_reboot()`. A host disconnect can therefore be followed by an
autonomous target reboot. Recovery must account for that behavior.

Missing output, wrong EL/CPU/MMU state, timeout, exception, transport loss or
unverified artifact identity remain failures or inconclusive observations.
Neither this packet nor a captured marker is a normal-kernel or Apple hardware
support claim.
