# B1: a boot chain with no controller

State: designed; the stage is implemented, built for aarch64 and tested on this host only.
**Nothing here has run on the M1.** Nothing is installed; the install procedure
(`INSTALL-PROCEDURE.md`) is not authorised and was not executed.

Paths below are relative to the lab root; `m1n1/` is `sources/m1n1-baseline` (121fcbf),
`launcher/` is `runs/m1-desktop-010/pcie-m2-host-001/lab/integrations/siliconforest/m1-bringup`.

## 1. What exists today

- **The machine's chain.** iBoot starts stage 1 (m1n1, installed with the boot policy of the
  Linux install; `m1n1-aurora 1.6.1`, `reports/sprint-007/ssh-target-inventory.json`). Stage 1
  carries a `chainload=<ESP partuuid>;m1n1/boot.bin` variable and loads that file from the EFI
  partition with m1n1's own NVMe and FAT code (`m1n1/src/payload.c:445`,
  `m1n1/src/chainload.c:259,287`). `boot.bin` is the stage 2: m1n1, then device trees, then U-Boot
  gzipped, then the kept lines of `/etc/m1n1.conf`; Asahi's `update-m1n1` writes it and keeps
  the previous file as `boot.bin.old`
  (https://raw.githubusercontent.com/AsahiLinux/asahi-scripts/main/update-m1n1).
  On this M1 `boot.bin` is already the lab's file: "candidate004", the pinned m1n1 with a
  10 s USB window before `payload_run`, followed by the original payloads (10.2 MB;
  `docs/M1_RECOVERY.md`, `integrations/m1-boot-window/CONTRACT.md`,
  `reports/sprint-008/candidate004-target-plan.json`; original kept on the ESP as
  `boot.bin.darwin-lab-original-54fa07e4f5cb`).
- **How m1n1 finds payloads.** Whatever follows the image at `_payload_start` is walked by
  `load_one_payload` (`m1n1/src/payload.c:285`): gzip/xz (inflated to the top of the heap),
  FDT, cpio, Linux `Image`, `key=value` lines; four zero bytes end the list. A kernel and a
  device tree make `payload_run` call `kboot_boot`, which sets `next_stage`; `m1n1_main` then
  tears down USB, display and MMU and jumps (`m1n1/src/main.c:326`).
- **How one m1n1 loads the next.** `chainload_image` (`m1n1/src/chainload.c:48`) copies the
  new image to its own base, moves SEPFW and a copy of the boot arguments behind it and sets
  `top_of_kernel_data` behind those; the new m1n1's heap starts there. **A size limit follows
  from this fork's checks**: an image that reaches past the loader's own
  `top_of_kernel_data` is refused when the loader's heap is in use
  (`m1n1/src/chainload.c:129`), and beyond that line lie iBoot's device tree and boot
  arguments, which no chainload moves. The line is the end of what iBoot loaded for stage 1,
  whose Mach-O reserves 64 MiB for payloads (`m1n1/m1n1.ld:9`). So **a stage loaded from the
  EFI partition can be about 60 MiB at most.**
- **What the launcher does for a native boot** (`launcher/host_progress.py` `run_worker`,
  `m1-native-boot/native_boot.py`, m1n1 `proxyclient/m1n1/hv/__init__.py:1612,1707`): see the
  table in section 4. The EL2 page is `m1-native-boot/stub/el2_drop.S`.

## 2. The stage

One image: **the stage m1n1** (the pinned m1n1 plus `patches/` and `src/`), followed by
**PDSF records** where m1n1 looks for payloads:

| record | content | in the RAM proof | installed (later) |
|---|---|---|---|
| `config` | text, `key=value` lines: `bootargs=`, `display=`, `usb-nodes=`, `sprr=`, `amx=`, `wdt=`, `reserve-mb=`, `serial=`, `xnu-base=`, `seed-hex=` (bench) | written by the launcher | a file's worth of text, edited with `pdsf_pack.py` |
| `kernel` | the MH_FILESET collection, gzip (33.9 MB -> 10.3 MB) | yes | yes |
| `ramdisk` | the RAM root, gzip (384 MiB -> about 92 MB), inflated by the stage straight to its place | yes | no: does not fit (below) |
| `end` | | | |

Header: `"PDSF"`, version, type, flags (gzip), CRC-32 of the stored bytes, stored size, size
in memory; records on 16-byte boundaries (`src/pdsf.h`). The stage checks every CRC before it
uses anything, and gzip's own CRC covers what it inflates.

`payload_run` asks `pdsf_boot()` first (`patches/0001`, 13 lines in `payload.c`). No PDSF magic
after the image: m1n1 behaves exactly as before. With it, `pdsf_boot` (`src/pdsf_boot.c`) either
sets `next_stage` or fails; it never falls through to another payload. A failure returns to
m1n1's "No valid payload found", which brings USB up and runs the proxy.

What `pdsf_boot` does, in order: scan and CRC; parse the configuration; refuse anything but
T8103; inflate the collection into m1n1's heap and read its segments and `LC_UNIXTHREAD`;
read SEPFW, TrustCache, preoslog, `uart0` from the device tree m1n1 was given; put the EL2
page (16 KiB, the bytes of `native_stub.py`, checked at build) into m1n1's heap with the UART
address and flags at 0x3f00; compute the layout; check it against RAM, the memory
controller's carve-outs and the firmware data still to be copied; load the segments at
`vmaddr - vmin`; copy SEPFW, preoslog, TrustCache; inflate the RAM disk; get 256 bytes from the
SEP; write the new device tree and boot arguments; start the secondary cores
(`smp_start_secondaries`), write each one's reset-vector register with the page's address,
call it into `hold_entry` and wait for its acknowledgement; optionally arm the watchdog; set
`next_stage` to the page's boot entry with XNU's entry and the boot arguments. m1n1's
ordinary next-stage path does the rest. No NVMe, no NVRAM, no USB on the way.

Two additions to m1n1 beyond the hook: `smp_get_impl_reg()` and `smp_forget_call()` (a core
parked in the EL2 page never returns to m1n1's dispatch loop; m1n1 refuses a next stage with
a call outstanding). The launcher found m1n1's spin table by pattern for this.

### Sizes
- Stage m1n1: 4.26 MB (4.19 MB of it is the unchanged m1n1 with its stacks and `.bss`).
- Stage + configuration + collection: **14.5 MB** (`out/sf-stage-kc.bin`). Fits the 60 MiB limit.
- With the gzipped RAM root: about 107 MB. Does not fit a chainload from the EFI partition.
  In the RAM proof the launcher loads the image, so the limit does not apply (section 7).
  Installed, a RAM root would have to be a separate file the stage reads from the ESP (m1n1's
  NVMe and FAT code, as stage 1 uses for `boot.bin`; `boot_storage.c` already does this for
  Linux kernels). That is the one place the stage would touch NVMe, and it is not built: the
  rule of this task forbids it, and the real answer is a root on disk (SF-R).

## 3. Selection, with Linux as the default

Constraints: nothing may write NVRAM; stage 1 and the boot policy are not touched; a failed
SiliconForest boot must give Linux on the next power cycle with nobody repairing anything.

**Layout on the EFI partition**

| file | what | changes when |
|---|---|---|
| `m1n1/boot.bin` | stage 2 for Linux, as today, with a **selector** in its m1n1 (about 60 lines) | m1n1 changes (rarely) |
| `m1n1/sf.bin` | the SiliconForest stage image (stage m1n1 + config + collection) | every kernel build |
| `m1n1/sf-next` | empty marker: "next boot: SiliconForest" | written and removed by Linux |

A second image next to `boot.bin`, not a payload inside it: the file Linux boots from changes
once and stays small; kernel iterations replace only `sf.bin`; and if a Linux package update
runs `update-m1n1` and replaces `boot.bin` with the distribution's, the selector is simply gone
and the machine boots Linux.

**The choice** is made by the selector in stage 2, before `payload_run`, and is one-shot:

```
errors = PMU boot-error count          (SPMI, read only: pmic@f reg 0x9f02 bits 0..3)
if errors == 0 and ESP has m1n1/sf-next and m1n1/sf.bin loads:   chainload sf.bin
else:                                                             Linux, as today
```

- Arming: Linux creates `m1n1/sf-next` and restarts (`reboot`).
- Disarming needs no write by m1n1 and no NVRAM. Every boot that ends in a reset without an OS
  clearing it leaves one boot error in the PMU (SF-T: `macsmc-reboot: PMU logged 1 boot
  error(s)` after every native boot); Linux clears the count when it boots (cells in
  `sources/gpu-plan/linux-aurora/arch/arm64/boot/dts/apple/t8103.dtsi:1222-1236`). So after
  any SiliconForest session that did not end in an orderly restart (a hang, a panic, a held
  power button, the watchdog, and today every session, because this kernel cannot yet clear
  the count), the next boot sees `errors >= 1`, **ignores the marker and boots Linux**; Linux
  clears the count and its boot unit deletes the marker.
- The count never grows past 1 in this scheme: SiliconForest is entered only at 0, and the
  boot after it is always Linux. That answers the worry about what the firmware does at a
  higher count by never getting there.
- With the marker absent the selector costs Linux one SPMI read. The ESP is only read when
  the count is 0: NVMe initialised and shut down once more, as stage 1 already does on every
  boot before it hands over.
- A key held at boot was considered and rejected for now: m1n1 has no keyboard driver for
  this machine (SPI HID) and the power button's state is behind the SMC. A file is also what
  a later "boot SiliconForest" menu entry in Linux would write.
- Later, when the kernel can restart the machine and clear the count itself (SF-T "Between
  this and booting from the internal disk", items 3 and 4), a second marker `sf-default` with
  the same rule gives a machine that stays in SiliconForest across clean restarts and still
  falls to Linux after one bad boot.

**Not verified, needed before the selector is written:** that the count m1n1 reads is 0 after
a clean Linux restart and 1 after a reset session. Both can be read over the proxy at the
start of an ordinary bench boot (read only, RAM only). If the count turns out to be unusable,
the fallback design is a selector that renames the marker itself (a FAT write by m1n1), which
this design avoids on purpose.

**A failed stage.** The installed `sf.bin` is to restart the machine when `pdsf_boot` fails
(`flush_and_reboot`; a build switch, not written yet) instead of sitting in the proxy: one
boot error, next boot Linux. With
`wdt=<s>` the stage arms the SoC watchdog before the jump as a dead-man for a kernel that
hangs; whether this M1 takes a watchdog reset is not yet known (SF-T, "How the M1 is reset").
The bench build keeps the proxy fallback: a failed stage shows the m1n1 USB device and the
unchanged controller resets it.

## 4. The device tree: our edit

| launcher / hypervisor edit | ours (`src/pdsf_boot.c`, `src/pdsf.c`) |
|---|---|
| product names get " on m1n1 hypervisor" (`setup_adt`) | **not done**: the names stay Apple's |
| `/defaults/serial-device` = `uart0` phandle | same, while the console is the UART (`serial=0` in the configuration leaves it) |
| nodes of the USB port the hypervisor uses removed (18 paths, `dfp-endpoints` of `display-crossbar*`) | **`usb-nodes=keep` is the design**: the port is the OS's. For the first proofs `usb-nodes=remove<n>` reproduces the tree every boot so far had; the port number comes from the launcher (the proxy's port). |
| CPUs pruned to the running one when `cpus=1` | same rule, from `cpus=` in `bootargs` |
| `memory-map`: SEPFW, TrustCache, DeviceTree, BootArgs, preoslog, Kernel-mach_header moved (`load_raw`, `load_payloads`) | same values; `RAMDisk` added as two u64 when there is one |
| `__OS_LOG` removed from `segment-names`/`segment-ranges` under `/arm-io` | same (this tree: `aop` and its nub) |
| `/chosen/random-seed`, 256 bytes from the controller's CSPRNG | 256 bytes **from the SEP** (below) |
| `/chosen/kernel-ctrr-to-be-enabled` = 0 | same |

The tree is not patched in place: it is copied node by node into its new place with the edits
applied (`pdsf_adt_rebuild`), because m1n1's own ADT code cannot add or remove anything.

**Debug USB port.** Keep its nodes. m1n1 never brings USB up on the stage's path, nothing in
SiliconForest binds to `usb-drd`, `atc-phy`, `dart-usb` or `hpm` today, and the first USB driver
will want them. One real difference to check when `usbkeep` is first booted: with `atc0` back
in `display-crossbar0`'s `dfp-endpoints` the DCP driver sees a second endpoint.

**Entropy.** m1n1 asks the Secure Enclave's ROM for random words (`sep_get_random`,
`m1n1/src/sep.c:32`); it seeds Linux's `kaslr-seed` and `rng-seed` this way on M1
(`m1n1/src/kboot.c:166,658`), so the installed chain exercises it on every Linux boot. The
stage takes 256 bytes and refuses to boot if it gets fewer; nothing is padded or repeated.
iBoot's own `/chosen/random-seed` is 64 bytes, too short for this kernel. `hostseed`
(`seed-hex=` in the configuration) is a bench fallback only.

## 5. Memory

- The launcher gives XNU the RAM above m1n1's heap and its own 1 GiB host heap.
- The stage keeps the launcher's layout rules (`pdsf_layout`, compared with the launcher's
  formulas in `test_b1.py`) and chooses only where it begins: `phys_base` = the next 32 MiB
  boundary above everything m1n1 uses plus a reserve (64 MiB by default). Nothing else has to
  stay: after the jump m1n1 is dead; what remains resident is the EL2 page, with the held
  cores in it, below `phys_base`.
- In the RAM proof that is about 0.5 GB above the start of RAM instead of the launcher's
  1.3 GB (word `lowmem`; estimated from boot `nb08`'s addresses); installed, with a 14.5 MB
  image, about 0.25 GB. For the first proof
  the launcher passes its own `phys_base` (`xnu-base=`), so that every address, including the
  entry point, is the one of boots `nb04`..`nb08`.
- Getting the rest: XNU takes one range `[phys_base, phys_base + mem_size)` and never frees
  what lies between the kernel and `top_of_kernel_data`. So the EL2 page can live there, and
  the kernel can be given RAM from iBoot's own `phys_base` up. That needs the loaded range
  moved down over m1n1 after m1n1 has shut down: a copy loop in the EL2 page's boot entry, as
  m1n1's own chainload stub does. Not hard, not done: about 0.2 GB for one more thing to debug.
- For Linux, m1n1 does the equivalent with `/reserved-memory` and spin-table release
  addresses in its own image; XNU has no such table, hence "below `phys_base`".

## 6. End-of-boot signal without the UART challenge

Proposed, not built: the launcher puts a fresh nonce into the configuration
(`bootargs=... sfnonce=<hex>`); the guest's session script, once Wi-Fi has an address, sends
`nonce, uname, hw.memsize, kern.bootargs` in one UDP datagram (or one HTTP request) to the
workstation on the bench network; the workstation's listener records the datagram's source
address and time. A boot counts when the nonce comes back from the M1's Wi-Fi address inside
the time bound. The reset stays the controller's (on a timer after the datagram or at the
bound), and "the M1 returns to verified Linux" is checked exactly as now. This proves a
kernel, a root, a session started by launchd, the Wi-Fi driver and the IP stack; it does not
prove the console. Until the session starts without a typed command (SF-D D4) the UART
challenge stays, as in the proofs below.

## 7. The RAM proof (prepared, not run)

`tools/sf_stage_boot.sh <sfbNN>`; launcher mode `native stage ...` (`native_stage.py`,
one hunk in `launcher-native.patch`). The launcher collects the proxy as always, builds the
image on the controller (stage m1n1 + configuration + collection gzip + RAM root gzip, about
107 MB, 10 s at 11 MB/s), writes it into its own part of m1n1's RAM, and vectors m1n1 into
it at `base + 0x800` with a copy of m1n1's boot arguments whose `top_of_kernel_data` lies
behind the image. m1n1 runs where it was written (it is position independent; no copy to the
old base, so nothing of the running chain is overwritten). After the vector the launcher
sends nothing over USB; console and challenge are on the UART as in `nb04`..`nb08`, and the
controller's accounting is unchanged (`native-start.json` with `hypervisor: false`,
`vdm-return`).

## 8. What is not proven

Everything on hardware. In particular: m1n1 started at an address other than its
predecessor's base; the SEP giving 256 bytes at that point; `pdsf_boot.c` as a whole (it is
compiled, reviewed and never executed: only `pdsf.c` runs in the host tests); the selector
(not written); the PMU count as m1n1 reads it; the watchdog reset.
