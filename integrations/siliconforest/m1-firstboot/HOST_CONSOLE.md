# J293 bounded host console launcher

`host_console.py` is a review candidate for the M5 controller running macOS
ARM64. It drives an **already established** USB0/USB1 m1n1 proxy on J293/T8103.
Default invocation is an offline plan; even `--device` alone does not open it.
No hardware execution was performed while implementing or testing this file.
Proxy bootstrap, cable routing, transport, firmware compatibility and independent
recovery remain unqualified. The saved Linux inventory does not establish them.

Root's separate `reports/sprint-007/m5-host-setup.json` records the prepared
controller workspace `/Users/chriskearney/darwin-lab-firstboot-007`: Python
3.14.7, an isolated venv installed from hashed offline wheels, successful imports
of `HV`, `MachO` and `CPUFeatures`, and a 20-byte CPUFeatures structure. No serial
device was opened, HV initialized or target boot attempted by that setup.
These host results do not establish the live proxy ABI, transport or recovery.

The guest is the raw `DEBUG_ARM64_T8103` XNU Mach-O experiment described in
`CONTRACT.md`: `LC_UNIXTHREAD` selects `__start`, which branches to the bounded
first-entry routine. Its only intended reached checkpoint is
`DARWIN_LAB_M1_EARLY_CONSOLE`, followed by permanent parking. Normal kernel
initialization, VM, drivers and init have not started. This is an m1n1 **HV**
experiment; this launcher makes no bare-metal boot claim.

## Offline review

From a copy of this lab on the M5, using the interpreter/venv prepared by root:

```sh
python3 integrations/puredarwin/m1-firstboot/host_console.py
python3 integrations/puredarwin/m1-firstboot/host_console.py \
  --manifest /absolute/path/to/reviewed-packet/manifest.json \
  --host-source /absolute/path/to/extracted-pinned-m1n1-source \
  --inventory reports/sprint-007/ssh-target-inventory.json
python3 integrations/puredarwin/m1-firstboot/test_host_console.py -v
```

`--host-source` contains `proxyclient/` and `requirements.txt` at its root.
The packet's existing `m1n1_build_record` artifact must contain the exact
`proxy_source_sha256` map now recorded in `reports/sprint-007/m1n1-build-002.json`.
No packet artifact is added. Root's `tools/m1_firstboot.py` validates all artifact
hashes, successful build receipts, source patch selection, ARM64 MH_EXECUTE,
LC_UNIXTHREAD, the linked diagnostic branch and instruction prefix, and the
four linked cache symbols bound to the DAIF-mask / `x19=0xef` / self-loop
unsupported path **before any serial open**. The launcher calls that checker
and does not reimplement it. It then verifies every receipt-listed host file.
These are byte/receipt checks, not build attestation, parked-register capture,
or runtime proof.

The worker imports a fresh private copy containing only those verified source
bytes. Existing checkout bytecode and unlisted source files cannot enter that
copy. The Python interpreter and third-party venv dependencies remain root's
separate environment responsibility; the M5 setup record above supplies the
observed import evidence. Offline planning itself does not import m1n1 or
repeat that qualification. No source fetch, installation,
macvdmtool command or provisioning occurs here.

## Explicit execution contract — not an authorization to run now

After packet review and a separately authorized operator decision, the only CLI
execution form additionally requires all of:

```text
--execute
--device /dev/cu.usbmodem<explicit-reviewed-endpoint>
--confirm-recovery J293_RECOVERY_READY
--private-directory /absolute/private/location/new-run-directory
--timeout 60
```

The confirmation is an operator assertion that the packet has been reviewed and
an independent recovery path is available; it is not a machine-verifiable
recovery result. Do not treat the flag or this document as evidence of tested
recovery. The directory must not already exist and must lie outside the lab.
The launcher creates it with mode 0700 and records files with mode 0600. Do not
copy raw ADT, BootArgs, firmware metadata, serial output or full logs into Git.
`M1N1*` environment overrides are rejected. The endpoint is explicit, never an
environment/default device. Timeout is bounded to 1–300 seconds for the entire
worker, including import, metadata collection, image transfer and entry request.

The parent uses a spawned worker, not an optional unbounded CLI worker mode.
The worker starts a new host session with no interactive stdin. Timeout or
parent Ctrl-C kills **only that host worker**. No target reset, reboot, storage
write, cleanup or rollback command is issued. After capture, no continuation
command is issued either. Closing/killing the connection does not guarantee
that the target stays stopped in EL2 or restore controller state. Pinned
`src/uartproxy.c:283–290` returns -1 when the exception-proxy transport read
fails; `src/hv/hv_exc.c:70–88` handles that return with `flush_and_reboot()`.
A disconnect **can provoke an autonomous target reboot**. That source path does
not prove that every host close triggers it. Independent manual recovery is
needed either way. The worker exits after recording capture; keeping it open
until the parent's deadline would only postpone the same possible disconnect.
The launcher never
automatically retries, continues to a full OS, or treats a normal worker exit,
timeout or embedded/received marker as a hardware PASS. Exit status after an
execution request is nonzero; private evidence requires independent review.

The launcher now attempts one bounded exception-context capture. A console
fragment or timeout remains partial evidence. The capture records observations
and comparisons, never an automated boot verdict. Independent review must bind
the packet, full console line, original context and expected checkpoint; a
matching JSON boolean alone is not the `CONTRACT.md` checkpoint or a kernel boot.

## Checks and ordering

1. Repeat root's packet checks and host-source verification in the worker.
   Resolve pinned host imports before opening the explicit USB callout endpoint.
   Neither `m1n1.setup` nor `PMU.reset_panic_counter` is used. No baud/proxy
   bootstrap helper is called.
2. Read the live proxy base and compare its version-tag bytes at the offset in
   the hash-verified raw m1n1 image. The full BUILD_TAG must be
   `121fcbf35d4501814b58ca5347926a4173b6a512`, and CPUFeatures must use the pinned
   20-byte ABI. This observes a live tag, **not all live image bytes**.
3. Read original BootArgs/ADT. Require J293, MacBookPro17,1, ADT/proxy chip ID
   0x8103, UART0 at 0x235200000 and exactly one ADT CPU marked running. Validate
   BootArgs revision 1/2/3 and version 1/2, bounded DRAM/ADT/BootArgs ranges,
   nonempty SEPFW/TrustCache ranges, and the pinned loader's heap/guest layout.
   Firmware ranges must lie in bounded RAM and end before planned heap/guest
   writes. Pinned `memcpy8` copies bytes (`ldrb`/`strb`) and does not require
   8-byte address or size alignment. This launcher still requires a 16K-aligned
   proxy heap, 16K guest displacement, and a preoslog size that is a multiple
   of 16K so predicted BootArgs stay 16K-aligned. That BootArgs rule is a
   launcher selection. Stock `load_raw` adds the raw preoslog size and does not
   require it. Unsupported observations fail.
4. Use `tools/m1_proxy_preflight.py` to validate the snapshot. Its Linux-compatible
   strings and historical stage1/stage2 tags come from the hash-recorded saved
   Linux inventory, with provenance recorded explicitly. The live Apple ADT is
   preserved separately. Historical dirty tags are not compared to the current
   proxy; live BUILD_TAG plus packet/build/host-source pins must match. Live
   ADT field spelling/values still require qualification and are never replaced
   with inferred J293/M1 identity to force a pass.
5. Read `SCTLR_EL12` and `VPIDR_EL2`. Require MMU bit M=0 and Apple implementer
   0x61 with M1 part 0x22/0x23. Do not write these registers to manufacture a
   match. Save original ADT and BootArgs bytes, the snapshot, validation,
   predicted layout and packet identities privately before `HV.init()`.
6. Initialize pinned HV with `smp=False`, no watchdog CPU and
   `emulate_sprr=False`; the pre-VM experiment uses no SPRR/GXF and needs no guest
   instruction patching. Confirm the HV ADT contains only the running guest CPU.
   Set exactly:

   ```text
   -v debug=0x14e keepsyms=1 serial=3 serial-device-name=uart0 disable-uart-irq=1 cpus=1
   ```

   The current entry experiment does not parse those arguments. One guest CPU
   is enforced by the host/HV selection, not by trusting `cpus=1`.
7. Recheck guest MMU/identity, call `load_macho(BytesIO(verified_kernel_bytes))`
   without a kernel collection or separate symbol file, then compare the actual
   load addresses with the predicted layout and require 16K displacement. Check
   the guest registers again immediately before `HV.start()`. Guest shell,
   secondary-CPU start and host reboot entry points abort this bounded worker.
8. Guard the pinned transport and context callbacks before calling `HV.start()`.
   Arm `signal.setitimer(ITIMER_REAL, 10)` only when the exact outer
   `P_HV_START(entry, bootargs_pointer)` command has completed a full-length
   serial write and enters `iface.reply(REQ_PROXY)`. `hv.started` alone is not
   sufficient: pinned `start` sets it before sending that frame. There is one
   one-shot SIGALRM, no timer thread, self-PID signal, second reader or retry.

## Bounded context capture

The main-thread alarm calls the pinned `_handle_sigint`/`interrupt` path only
while waiting on the written outer frame, with reply depth 1, command and bulk
I/O depth 0, boot/event dispatch depth 0, `_in_handler=False`, and no previous
injection or close. Its single `!` is the pinned break-in byte. A short serial
write or unsafe boundary records an error and stops without a new interrupt
byte. In particular, an alarm during ordinary hook handling does not insert a
byte into a partial proxy frame. The timer is disarmed on every exit; the
parent's independent hard timeout still bounds imports, transfer, entry and
capture. A deadline shorter than preparation plus ten seconds can kill the
worker without any capture.

Ordinary `START.HV/HV_EVENT.HOOK_VM` callbacks before capture keep the pinned
MMIO service, context commit and `P_EXIT(HANDLED)` acknowledgement. They must
be allowed to complete for console/controller service. No such acknowledgement
is permitted after capture, after the interrupt request or outside that normal
hook path. Every other known exception-context callback is read through the
pinned `_load_context` reader and stopped before normal dispatch; PANIC and
VIRTIO carry `ExcInfo` even though their stock handlers use other paths.
Watchdog callbacks have no `ExcInfo` in the pinned source and remain explicit
missing-context errors. No context pointer or register value is invented.

The adapter preserves original `_info_data` bytes before any handler mutation
or commit. It writes `exception-context.bin` and `exception-context.json`
privately, recording ELR, full SPSR, x0, x19, CPU ID, event, raw-byte SHA256 and
capture boundary. Unknown callbacks, read/parse failures and an absent context
are errors; a previous hook's context cannot substitute for the current event.
The shell override and write guards prevent continuation even if a pinned
handler catches an exception. Host disk failures also escape through a bounded
stop; partial or absent files are not evidence of a checkpoint.

The comparison uses root's inspected `kernel.park_address`, never a marker
string search in the launcher:

```text
ELR = layout.guest_base + kernel.park_address - layout.vmin
x0  = layout.guest_base + layout.bootargs_off
x19 = 3
SPSR & 0x3df = 0x3c5                 # all DAIF masks + EL1h
CPU ID = original sole running CPU
event = START.HV / USER_INTERRUPT, after this worker completed its one injection
```

NZCV is retained but not compared: the diagnostic's `cmp`/`subs` instructions
change it. Pinned `src/hv/hv_asm.S:168–171` selects EL1h. Matching these conditions
sets only `expected_park_conditions_match`; `boot_proof` and
`hardware_boot_observed` stay false. The full console line still requires
independent review. An unexpected exception with readable context is retained
as diagnostic evidence and cannot qualify as the requested capture.

There is a residual receive-side window after the reply parser has consumed a
callback frame and before its dispatch wrapper runs. An alarm there can send
the one byte while a callback is already pending. If a HOOK_VM then arrives,
its original context is saved diagnostically and the guest is not continued;
the shot may be lost. There is no retry or claim of a guaranteed capture.
Actual USB behavior, EL2 tick delivery and parked-context acquisition remain
unqualified; these source guards do not establish runtime proof.

Reviewed pinned host locations: `proxy.py:208–217` (`cmd`), `265–329` (reply
and callback dispatch), `390–427` (request/bulk writes), and
`hv/__init__.py:930–1011`, `1937–1946`, `2001–2007` (context/continuation,
interrupt byte, and start ordering). The adapter does not use the ineffective
`pre_reply` parameter: pinned `_request` passes `pre_reply=None` at line 739.

## State changes and remaining limits

This is not a read-only device operation once explicitly executed. Even before
HV initialization, `ProxyUtils` reserves heap space and uploads/cache-maintains
small instruction stubs in target RAM for register reads. Source checks and
range checks precede that use, but neither proves arbitrary target RAM safe.

Pinned `HV.init/load/start` changes hypervisor registers/page tables, transport
usage, virtual UART/controller mappings and display state; it copies kernel,
ADT, BootArgs, SEPFW and TrustCache data into target RAM. HV changes product
strings and controller/CPU nodes in its ADT copy. Original metadata is retained
before those changes. HV installs essential PMGR/MMIO hooks and maps device
regions; this is not a general device-isolation proof. The one-running-CPU
observation and single guest selection do not attest that all physical host
CPUs are powered down. RAM carveouts, platform controller behavior and the
complete firmware profile remain runtime qualification concerns.

Source evidence: pinned `proxyclient/m1n1/proxy.py` implements CPUFeatures and
proxy base reads; `src/proxy.c` returns `_base`; `src/main.c` defines the version
tag; `m1n1.ld` makes raw file offsets relative to `_base`.
`proxyutils.py` supplies heap/register operations. `hv/__init__.py` supplies the
exact ADT, load-address, transport, framebuffer and single-CPU behavior used
here. See especially `init`, `setup_adt`, `load_raw`, `load_macho` and `start`.
None of these source observations establishes a reached XNU checkpoint.

The tests use nonbootable packet fixtures and fake host/device objects. They
import the current `tests/test_m1_firstboot.py` fixtures, including the linked
cache fail-stop bytes, and do not keep a private kernel image. They check
default/rejection behavior, source hashes, range checks, odd SEPFW/TrustCache
lengths under the byte-copy layout, collector field mapping, ordering and
parent timeout. Capture tests replace both the signal scheduler and transport:
they check frame/reply ordering, unsafe-boundary rejection, ordinary hook
service, original-byte preservation, missing/unexpected contexts, each register
comparison, no second injection, teardown and forbidden continuation. They do
not exercise real signals, pyserial, m1n1 imports on the M5, HV, UART, recovery,
parked PC/x19 or XNU runtime.
