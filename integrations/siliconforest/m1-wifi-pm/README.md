# Wi-Fi power-save candidate

FXNET's `cf9034f6b` adds boot defaults and a runtime power-save request to
SFBwfm. The new default keeps fast power save (mode 2), with
`pm2_sleep_ret=2000` milliseconds. It adds no polling thread or timer.
The existing firmware timer does the work. This trades a longer awake period
after traffic for avoiding a DTIM wait on replies arriving after 200 ms.
Idle power and real page-load improvement require measurement.

`fxnet-pm.patch` is exactly that commit's six driver-file changes, with paths
translated into the SiliconForest SDK tree. All six old files match the deployed
source byte for byte. The build freezes that tree, including its DART code.
A clean SDK rebuild reproduces the deployed raw SFBwfm executable byte for
byte; SHA-256 `d2ce5b1a189ea221437544ec7984ecd9b150e543153d5aa7ac0aa0e86e648a36`.
The userland tool remains FXNET's `m1-bwfm/tools/sf-wifipm`.

`pm-admission.patch` adds three narrow corrections:

- Reject unknown request versions, modes and timeout ranges before `chipUp`.
- Return EIO if the firmware rejects the timeout, even if the mode command
  succeeds. This can be a partial update; GET reports desired and actual
  values separately, and desired settings remain available for the next init.
- Preserve the boot/default timeout on the first offline mode change with
  timeout zero. Zero means preserve, rather than erase, the current target.

The request is a SiliconForest interface, not Apple's private ABI. SET takes
version/mode/timeout (12 bytes); GET returns those plus two signed firmware
readbacks (20 bytes). Unavailable firmware values are -1. `bwfm_pm=0|1|2`
and `bwfm_pm2_ret=0..60000` set boot defaults. Boot timeout zero leaves the
firmware's own value; runtime timeout zero preserves the cached target.
Use `sf-wifipm fast 200` to reproduce the previous timeout and `fast 2000`
to restore the new default without rebooting. `off` selects constantly awake.
A successful init log alone does not prove the timeout iovar was accepted:
check for the refusal log and obtain firmware GET readback.

`test-pm.py` extracts the actual patched boot, init, setter/getter and request
admission code and compiles it with ASan/UBSan against fake firmware/locks.
It checks default/range behavior, AP init, command ordering, running/offline/
bus-gone states, firmware failures and malformed requests before power-up.
These checks do not establish real firmware acceptance or concurrent hardware
lifecycle safety. SDK builds compile the full real driver.

`build-kc.nix` preserves qualified kernel 46 and changes only SFBwfm: 57
executable modules, 56 identical UUIDs including the core; only one kext
source changes. PCI/USB selections, original AVD, pinch/HID and killable waits
remain as qualified. No AVE or dock hardware trial is included. The candidate
keeps the same 599-byte command line, including `sfm1tpscroll=20`.

`build-qemu-image.nix` links the actual PM driver and its existing IO80211Family
into a disposable virt collection with the upstream PCI/USB and ext4 rehearsal.
QEMU has no matching Broadcom device: boot/USB/storage results cannot establish
Wi-Fi firmware behavior or M1 DMA/interrupt correctness. Private receipts are
under `runs/sftb/wifi-pm/`; installation and native results are recorded there
and in the coordination status, separately from source/build claims.

```sh
python3 integrations/siliconforest/m1-wifi-pm/test-pm.py
nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/m1-wifi-pm/build-driver.nix --arg port false --no-out-link
nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/m1-wifi-pm/build-kc.nix --no-out-link
python3 integrations/siliconforest/upstream-12377/run-qemu.py --image-recipe integrations/siliconforest/m1-wifi-pm/build-qemu-image.nix --output runs/sftb/wifi-pm/new-qemu-run
```

## Native qualification: kernel 47, 2026-11-16

Installed through the normal SFBOOT installer, retaining qualified 46 as
rollback. Firmware GET reports fast mode and `pm2_sleep_ret=2000`; runtime
changes to 200 and back to 2000 are read back correctly. No init refusal was
logged. The setting was restored to 2000 before release.

Same-boot workstation-to-M1 ping at one-second intervals, 18 replies per
setting, discarding the first three:

| Firmware timeout | Median | Maximum | Loss |
| --- | ---: | ---: | ---: |
| 200 ms control | 179 ms | 289 ms | 0/18 |
| 2000 ms candidate | 5.85 ms | 6.66 ms | 0/18 |

This establishes the radio latency improvement in this short comparison.
Ambient traffic was not controlled. It does not establish real page-load
speed, idle power, Linux parity or long-term stability; FXNET owns those
follow-up measurements.

Five GL-hash and five Vulkan checks passed. Clone slots returned to three
used, zero failures (519 allocations, 516 frees, peak seven). Three desktop
and three SSH Firefox launches mapped and painted the correct unique local
page, readable text and all three colour regions; all six captures were
visually checked. No synthetic input was used. The current AppKit,
WindowServer, QuartzCore and Firefox launcher hashes stayed unchanged during
the kernel test. These are paint checks, not general website/video/FPS proof.

An initial desktop launch job exited with status 1 before starting the session.
The supported `launchctl start org.siliconforest.desktop` retry restored the
standard DESK10 `.next` desktop and responsive stats. That command's reply
reported a connection reset even though the job started. The original startup
failure remains unexplained and was filed with DESK; it is not a boot-autostart
pass or attributed to the Wi-Fi patch. No kernel panic or external reset was
required. The read-only Linux filesystem check returned 4 with the retained
error classes; there were no new findings in comparison with the prior check,
no repair and no whole-root cleanliness claim.

Desktop-route owned process identities disappeared within 1.33–1.38 seconds
of quit. SSH-route checks retained only the direct launcher PID after ten
seconds, but the parent harness had not polled/reaped its Popen child and did
not record process state. A zombie is possible; this does not prove a stuck
client or qualify its exit status. The raw cleanup attempts remain recorded.
UART capture was stopped, all install locks released, and kernel 47 left
normal with the standard desktop responsive. Private receipts:
`runs/sftb/firefox-install-0047/`.
