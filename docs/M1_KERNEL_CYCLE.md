# Bounded M1 kernel cycles from the M5

`tools/m1_kernel_cycle.py` coordinates the reviewed J293 window, exact121 RAM
bootstrap, parked XNU experiment, and one external reset. It defaults to an
offline plan. The reviewed controller completed three consecutive cycles on the
real M1 from one M5 invocation: exact diagnostic console line and original parked
context, one external reset, then a verified new Linux boot. No intervention was
needed between cycles. See `reports/sprint-008/three-unattended-cycles.json`.
Its 31 host tests separately establish ordering and refusal behavior. A deliberate
one-second guest-worker timeout also recovered automatically; see
`reports/sprint-008/host-timeout-recovery.json`.

The fixed profile is the M5 Darwin arm64 host running as root, the dedicated
strict-known-host SSH key pair under `controller-ssh-008`, and the selected M1 at
`10.1.1.127`. SSH config, agent, multiplexing, forwarding, global known_hosts and
host-key updates are disabled. The private key is stat-checked but never read by
Python. This tool has no Maclab service/API dependency or submission path. Linux
preflight checks that the four previously isolated lab services remain inactive.

## Sequence and evidence

1. Hold `/Users/chriskearney/darwin-lab-firstboot-007/m1-controller-008.lock` across
   every cycle. Refuse a prior incomplete-cycle latch. Verify local helper hashes,
   the native reset binary, packet receipts/bytes/entry checks and pinned host
   sources before SSH or device access. Bootstrap is fixed to `350cd127…`, guest
   launcher to frozen007 `a9f68695…`, kernel to `d6c7ba75…`, and installed window004
   proposal to `e4ead838…`. No helper or payload is patched by this controller.
2. Observe known Linux `7.2.11-6-1-ARCH`, J293/T8103 compatibles, candidate boot-file
   hash, inactive services, writable `/`, `/boot`, and vfat `/boot/efi`, plus
   the known 30-second runtime / 2-minute reboot watchdog settings. Save the
   full baseline observation and boot UUID privately and durably. Create the
   incomplete latch and durable reboot intent before requesting orderly reboot.
3. Poll native CDC for up to 90 seconds using exactly
   `ioreg -a -r -l -c IOUSBHostDevice`. Empty stdout or an empty plist is normal.
   Match VID:PID `1209:316d`, location `0x100000`, then the descendant serial
   `IOCalloutDevice` under primary data interface **1**. Reject interface 2/3
   ancestry and multiple matching endpoints. Interface 3 belongs to VUART.
   Filename order, suffix and the paired `IODialinDevice` are not selectors.
   Record the matched USB device's integer `IORegistryEntryID` and `sessionID`;
   missing or malformed identities refuse selection.
4. Run the verified bootstrap CLI with its 120-second worker limit. Require its
   supervisor to exit with a recognized result and an integer worker exit code.
   Require `vector-ack.json`; acknowledgment alone is not exact121 runtime proof.
   Then perform **fresh CDC queries for up to 25 seconds**, requiring a changed
   `(IORegistryEntryID, sessionID)` tuple. The same path with a new tuple is valid;
   a different path with the old tuple is stale and keeps polling. Enumeration
   freshness is not exact121 runtime proof. TTY initialization text is not a tag check.
5. Run the frozen guest CLI with its 60-second worker limit. Its existing live
   exact121 tag, CPUFeatures, board, BootArgs, firmware and guest-register checks
   remain mandatory before HV initialization/entry. This controller adds no proxy
   commands or second serial reader.
6. Record `ENTRY_PARK_OBSERVED` only if the private worker log contains the exact
   complete line below (optionally prefixed by the pinned `TTY> ` wrapper), and
   `exception-context.json` has boolean `expected_park_conditions_match: true`.
   Also check raw-context hash, injected USER_INTERRUPT identity, expected versus
   observed registers, and packet/layout binding. ELR must be the inspected park
   address translated through the actual layout; x0 is its BootArgs pointer,
   x19 is 3, SPSR masked with `0x3df` is `0x3c5`, and CPU matches the saved running
   CPU. This is a bounded pre-VM entry/park observation, never full XNU/init success.
7. Stop all acquisition polling. If no bootstrap child was attempted, wait for
   Linux fallback without a PD reset; timeout or a rejected reboot with the same
   boot UUID stops for review. Once both attempted child supervisors have
   established that their workers terminated, check for Linux first. A verified
   new Linux boot needs no reset; a responding but unexpected Linux also stops
   without reset. If SSH fails, take a fresh primary CDC observation. A single
   failed SSH probe is not evidence that the target is still a proxy. Permit the
   pinned `macvdmtool reboot serial` **once** only if the selected native primary
   CDC is still present; include that observation in the durable reset intent.
   With no primary CDC, wait for possible autonomous Linux return with **no PD
   reset**, stopping incomplete if Linux never verifies. Poll Linux for 150 seconds. If SSH is still
   unavailable after 25 seconds, permit one selected `arp -d 10.1.1.127` attempt.
   Its mechanism remains unproven; it is not a broad cache flush or reset retry.
8. Require a new boot UUID with the same known Linux kernel, installed candidate
   hash, board, all three writable mounts and the same known watchdog settings.
   Save the returned observation before clearing
   the incomplete latch. Repeat only after both entry/park and Linux-return
   observations, with no experiment/recovery error. `--cycles` permits 1–3.

```text
HVLOG: DARWIN_LAB_M1_EARLY_CONSOLE checkpoint=entry-before-vm next=park kernel_init=not_started
```

The console matcher follows pinned `src/hv/hv_vuart.c`'s HVLOG passthrough and
`proxyclient/m1n1/proxy.py`'s `TTY> ` prefix. It requires a complete newline-ended
line; partial markers, embedded strings or unrelated initialization lines do not
qualify. Context comparison follows frozen007's mask, preserving NZCV separately.

## Failure and ownership boundaries

Each CLI owns its existing worker hard timeout. The controller's outer limits
are 150 seconds for bootstrap and 90 seconds for the guest. If an outer CLI hangs,
is interrupted, or lacks a recognized completed-worker record, **no reset or
further hardware command follows**. The controller does not kill that supervisor
and accidentally orphan its separately sessioned device worker. The child CLI
inherits the target lock descriptor, so it retains the lock if still running.
The fixed `m1-kernel-cycle-incomplete.json` latch also blocks a later invocation.
Independent inspection/recovery must establish worker termination and target
state before an operator clears that latch; there is no force/resume switch.

A known experiment failure with terminated workers first checks for Linux; one
recovery reset is permitted only after a bootstrap attempt, unavailable SSH and a
fresh matching primary CDC observation. Without CDC, only bounded Linux-return
polling and the one selected neighbor refresh are permitted.
The failed experiment stops even if Linux returns. Reset failure, missing Linux return, changed
kernel/loader/mounts or failure to clear the latch also stops without repetition.
Closing a proxy descriptor can provoke autonomous target behavior, including
reboot on pinned error paths; the controller does not promise an EL2-stopped
target. The tested reset path covers this pinned diagnostic and the injected
worker timeout; arbitrary firmware/USB failures, controller power loss and other
kernels are not qualified by these runs. No stage1, boot-policy, boot-storage or
watchdog settings are changed by the cycle controller.

The native reset command has a 12-second subprocess deadline. On timeout,
`subprocess.run` kills that host tool; it may not leave ACE debug mode cleanly.
The private timeout record and cycle result mark ACE and target-reset state as
unknown. No reset retry, mode-cleanup command or further recovery polling follows
that timeout; independent inspection is required.

The shared lock matches `tools/m1_recovery.py` and the reviewed private helper.
Direct installer/bootstrap/guest invocations must also be enclosed by that M5
lock; their frozen code is not modified to acquire it. The incomplete latch is
specific to this controller; other tools do not interpret it automatically.

All observations, boot UUIDs, subprocess output, topology and context remain in
unique private run/cycle directories. Files are mode 0600 and the run root must
be root-owned mode 0700, outside the repository and SSH-key directory. Intent
records fsync both file and directory before reboot/reset. Public stdout contains
only the plan or a bounded-cycle summary, with `full_xnu_boot_claimed: false`.
These fsync calls are not macOS `F_FULLFSYNC`; M5 power-loss durability remains
unqualified.

## Review and host-only tests

```sh
python3 tools/m1_kernel_cycle.py --cycles 1
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tests.test_m1_kernel_cycle -v
```

On the prepared M5, the existing venv Python must launch the staged script.
Execution requires `--execute --confirm-recovery J293_RECOVERY_READY`; review
all three files and the fixed hashes before supplying these flags. Defaults use
the existing `packet-r6`, `m1n1`, and `window-004` directories under the prepared
workspace. Alternate input paths still undergo the same fixed-byte verification.
The implementation agent used host tests; the project lead subsequently ran the
hardware qualification with the exact controller hash recorded in the runtime report.

The qualification command launched on the prepared M5 is:

```sh
sudo -n /usr/bin/caffeinate -i -s \
  /Users/chriskearney/darwin-lab-firstboot-007/venv/bin/python3 \
  /Users/chriskearney/darwin-lab-firstboot-007/lab/tools/m1_kernel_cycle.py \
  --execute --confirm-recovery J293_RECOVERY_READY --cycles 3
```

`caffeinate` holds sleep assertions for this invocation and releases them when
it exits. The M5 remains the controller, and the M1 remains the target. See the
runtime qualification reports under `reports/sprint-008`; source review and
host tests alone do not establish the command's hardware result.
