# M1 unattended boot qualification — increment 008

The target is the 13-inch M1 MacBook Pro (J293/T8103). The M5 is the
controller. This workflow has no dependency on Maclab, its server, or its jobs.
The owner approved the first installation; candidate004 is now installed and
its first Linux fallback boot returned successfully in 45.552 seconds. Native
CDC acquisition, an exact pinned RAM loader, and one real M1 XNU diagnostic
entry under m1n1 HV have now been observed. The diagnostic printed the expected
checkpoint and parked before XNU VM initialization; the M5 reset it and verified
a new Linux boot. The reusable controller then completed three consecutive
cycles in one invocation with no intervention between cycles; a deliberate
one-second host-worker timeout also recovered to Linux.

## Demonstrated control

The standalone M5 USB-PD tool has reset the M1 after three orderly Linux halt
requests and observed 40/60/90-second periods without SSH. Linux returned on
all three attempts without physical intervention. Serial communication works
in both directions. These observations qualify that tested reset path, not
arbitrary firmware failures, a powered-off machine, or a Darwin guest.

Records: `reports/sprint-008/recovery-three-cycles.json`,
`serial-bidirectional.json`, and `maclab-isolation.json`.

## Reviewed first installation proposal

Candidate004 adds a ten-second native USB proxy acquisition window to a
separate m1n1 build based on commit
`121fcbf35d4501814b58ca5347926a4173b6a512`. On expiry it prepares the original
Linux payload, restores the saved USB-C controller interrupt masks, and hands
off normally. The timer starts after USB initialization; it cannot recover a
hang inside that initialization.

| Artifact | SHA256 |
| --- | --- |
| Original complete boot.bin | `54fa07e4f5cbeecd90e67e335657c40b33c25aede7015a84afb1427333b4f923` |
| Candidate004 complete boot.bin | `e4ead838bb87a04dd00909edf8a598f813056ee2f9611777a4a77c4bf7351e27` |
| Candidate004 raw prefix | `ba5bf4b396168ac540df852e3507c4342dd289eef7bf2c1fc29c50f11f0822bf` |
| Unchanged Linux payload suffix | `4013e6ad437923b9d8ffdb1b6e72fa6fea03decd413fb7a288785c25e6451684` |

The proposed file is 10,238,674 bytes. Its raw prefix footprint is 0x420000;
the original is 0x400000. The 5,913,298-byte DTB/U-Boot suffix is identical.
The build and packaging record is
`reports/sprint-008/window-candidate-build.json` (attempt004).

The candidate and `tools/m1_boot_install.py` are staged in
`/home/chrisk/darwin-lab-control-008/window-004/` on the M1 and
`/Users/chriskearney/darwin-lab-firstboot-007/window-004/` on the M5.
The original backup is also on the builder and in the M5's private
`recovery-original-008/` directory. No private key or firmware is in the lab.

The M1 plan-only check verified the live ESP PARTUUID against the chosen
device-tree ESP, the original stage2 version and boot-file hash, the exact
candidate and suffix, and 182,812,672 free bytes on the ESP. This is a plan,
not an installation or boot observation.

## Completed first setup and rollback

There is no qualified way to enter the installed loader's proxy and try this
candidate entirely in RAM. The owner approved the first persistent prefix boot
and was available at the M1 for recovery. That first boot returned normally.
A failure before the window opens can make USB-PD resets repeat the same bad
prefix. Remote reset alone does not provide an independent disk rollback.

The installer preserves the existing `boot.bin.old` and creates a separate,
verified ESP backup:
`/boot/efi/m1n1/boot.bin.darwin-lab-original-54fa07e4f5cb`.
It changes only stage2 `m1n1/boot.bin`, that new backup, temporary staging
files, and a private operation journal. It does not change stage1, partitions,
NVRAM, firmware, or boot policy. FAT replacement is not power-loss atomic.

Before installation, stop boot acquisition and hold the M5 target lock; do
not issue resets while the ESP is being written. Run the exact reviewed
installer with a fresh operation ID, verify its completed result and storage
re-read hashes, then perform the first orderly reboot with no proxy probe. Any
nonzero installer exit is a do-not-reboot condition requiring inspection, even
when it reports a generic refusal or an incomplete operation.

If Linux returns, verify the new loader version/hash and normal Linux health.
The installer's `--action restore` path can restore the verified original
while Linux is running. A restore is also followed by an orderly reboot and
verification; a successful copy alone is not a successful boot.

If Linux does not return and independent reset repeats the failure, stop
automated retries. The owner must use the startup options/recovery path,
mount the identified ESP, and copy the dedicated original backup over
`m1n1/boot.bin`. Check the backup hash against the table first and sync the
volume before rebooting. The M5 copy is an additional recovery source. Do
not select DFU restore or change boot policy as an automatic fallback.

## Qualification progress

1. Complete: candidate004 booted Linux without a host probe.
2. Complete: native USB proxy acquired, followed by an independent reset and Linux return.
3. Complete: the exact unmodified pinned m1n1 image ran from RAM, with live
   tag, board, BootArgs and ABI verified; reset returned Linux in 39.377 seconds.
4. Complete: the bounded XNU entry diagnostic emitted the exact checkpoint,
   and its original exception context matched the expected parked state.
   Independent reset returned to a new Linux boot in 60.564 seconds.
5. Complete: the reviewed reusable controller passed three consecutive cycles,
   each with checkpoint/context evidence, one external reset and a verified new
   Linux boot. An injected one-second worker timeout was classified as a failed
   experiment and recovered automatically. See `reports/sprint-008/three-unattended-cycles.json`
   and `host-timeout-recovery.json`.

The M5 owner enabled its documented accessory access setting after macOS
enumerated m1n1 but blocked functionality. USB selection now uses VID/PID,
physical location and the primary CDC interface; it does not choose the first
modem glob. A bounded refresh of only the M1's network-neighbor entry restored
M5 SSH after reset. The underlying network cause remains unproven.

The native kernel diagnostic is designed to print an early entry checkpoint
and park before VM initialization. It is not a native Darwin userspace boot,
an Apple GPU/storage driver, or safe mapped-memory reclamation.
