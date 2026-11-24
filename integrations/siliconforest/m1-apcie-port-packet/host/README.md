# Isolated read-only APCIE observer host gate

Source-only isolation for the next separately reviewed port observation packet.
Canonical host `0cf9ccc9bbb966dbf43d707488956baede0cd917ffce08ce3260ff94cc9a946f`
and controller `cce9803f0c63b6c6b3a8cf62d0e7374566e01429e04877945ccad09f1184077a`
remain unchanged. This leaf does not derive from the serial network host.

`host-binding-001.json` pins all 16 copied source/data files in
`runs/m1-desktop-010/apcie-port-host-001/lab`, plus their canonical inputs. Only
host_progress.py and m1_bringup_cycle.py differ. `overlay/` preserves those two
complete resulting files; `prepare.py` reproducibly checks the exact old hashes
and applies unique anchored changes. It refuses an existing destination. Do not
run it to replace this frozen tree. No packet/KC/RAM image is assembled here.

The new build-record field must be exactly:

```json
{"apcie_port_observer_experiment":{"version":1,"observer":"PDApcieT8103Bridge","snapshot_bytes":84,"read_only":true,"shared_writes":false}}
```

Integer and boolean types are checked independently of Python equality. Extra
keys, null/false opt-ins, duplicate JSON keys, all conflicting experiment keys,
and network/terminal/virtual/keyboard/DART/survey/trackpad-enumeration fields are
refused. Existing exact `desktop_experiment` and `native_trackpad_child_experiment`
remain mandatory. The underlying native-child gate still forbids feature writes
and injected host HID. Removing the observer opt-in while retaining its reported
KC member is refused.

Both parsed kernel report and actual LC_FILESET_ENTRY names must contain exactly
the existing native50 names plus one `org.puredarwin.driver.PDApcieT8103Bridge`.
The baseline names were extracted by the unchanged image inspector from the
saved passive-child001 kernel `02fe2d4106ca3091b95a3e9235e3f9a7226c0f030eb24fb609f6817506c0f883`;
`native-members.json` records that provenance. This is a member-name constraint,
not permission to replace a kernel member. Exact new bundle/KC/kernel/source/RAM
hashes remain the future packet's separate responsibility. PDSerialHID,
PDDARTSurvey, PDGPUPreflight, duplicates, missing and substituted names fail.

Only the successful exact gate appends `pdapciebridge=1` to the unchanged native
child arguments. `verify_inputs` checks it before the controller can admit an
attempt, and `guest_bootargs` checks it again in the worker. Exact runtime120 /
worker240 is enforced in the controller's real `verify`, host supervisor, and
worker. Canonical controller timeout240, supervisor bounds180/300, and frozen
recovery authorization remain unchanged; no duration extension is added.

ConsoleSession, ConsoleProof, framebuffer geometry, strict challenge, optional
bootstrap and the single fixed `/usr/bin/darwin-desktop-test` launcher are
unchanged. The native writer seals immediately after that launcher. No DATA
bridge, network scheduler, new VUART send path or HID scheduler is added. The
legacy helper modules are copied solely because the unchanged canonical module
hash-checks/imports them; the admitted native-child path does not activate them.
Legacy profiles without the observer retain their original behavior.

Run `python3 -B integrations/puredarwin/m1-apcie-port-packet/host/check.py`.
It verifies exact isolation/overlay reproduction, runs 13 new groups and 58
unchanged legacy tests, and kills 12 syntax-valid semantic mutants in disposable
source copies. Actual controller.verify, supervisor.main, run_worker and
verify_inputs Python functions execute offline with synthetic packet verifiers
and explicit barriers before any device command, backend open or process spawn.
These are control-flow tests, not actual packet verification or device trials.
The sealed-writer tests use real temporary durable logs and a fake device.

The controller still has hardware capabilities for an eventual root-reviewed
packet. This source receipt does not authorize invoking them. No deployment,
SSH, service, hardware access, PCIe initialization, shared PHY/PMGR write, GPIO,
DART/MSI change or GPU observation occurred. PCIe boot remains held pending root
packet review; GPU observer HOLD remains absolute. The spare fake BCM4378 chip
transport is outside this leaf and was not modified.
