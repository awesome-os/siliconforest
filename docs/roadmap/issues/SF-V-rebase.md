# SF-V: rebase onto upstream's current development tip

## Goal
The lab builds from PureDarwin `e7aa00a6` (6 November 2026, the **pin**). Owner decision,
11 November: move to upstream's current tip, side by side. Everything that works on the pin keeps
working and stays the default until the rebased build is proven equal in QEMU and on the M1; the
owner switches the defaults.

- **Tip (fixed for this work):** `c713d8fb9b048a38e2242d67e29b9957ced8c495`, branch
  `upstream-xnu-12377`, committed 11 November 2026 21:21 -0500 ("ext4: extent index keys, cluster
  I/O, journal fixes"). corecrypto submodule `f577aafe` (pin: `ea8e6afe`).
- **Clone:** `sources/puredarwin-rebase` (this stream's own; branch `siliconforest-rebase` at the tip
  commit, submodule with its own objects). `sources/puredarwin-upstream-next` belongs to the Cocoa
  stream and is not touched. Upstream is not followed past this commit unless the owner says so.
- **Delta pin to tip:** 119 commits. Flake inputs `nixpkgs`, `kc-tools`, `iig-tools`, `xnu-loader`
  are the same revisions; the cross toolchain is rebuilt only because the SDK changed (no LLVM
  build: `arm64-cross-toolchain` is 14 small derivations).

## The tip is the default (switch of 12 November; owner: "Finish rebase")
**What a boot gets now when nothing names anything else**
- Source: `integrations/puredarwin/rebase/pd.nix` defaults to the tip `c713d8fb`; `SF_PD=pin` is `e7aa00a6`.
- Collection: `tools/default_kc.sh` (one place; every wrapper asks it) =
  `rebase/build-kc.nix` = `/nix/store/71zynndd213vab1q6iwbqx4bc3f23f8b-puredarwin-kc-arm64-0.1`:
  the collection proven in `rb01`..`rb12` plus, folded in, the no-recovery kernel patch
  (`m1-native-boot/kernel-patches/0001`, `IOSetRecoveryBoot` logs and returns) and `PDAppleAVD`
  (inert without `pdavd=1`). Variants: `default_kc.sh native`
  (`/nix/store/xwj7sc8c0rpq65cnm9hmp4db2x4gfhi3-…`, the platform expert with the held-cores patch),
  `storage` (`/nix/store/gjxjz2rpp2bzgrg0qf406rmf3pwidsa4-…`, + PDAppleNVMe; built, never booted),
  `reboot` (`/nix/store/mx63hv3mz6anxryhg7rhv8n04ap20vp5-…`, PDCpuPower with SMC restart; built,
  not booted).
- The 15-recipe chain: every collection and kernel recipe of it (`m1-boot-speed`, `m1-touchbar`,
  `m1-sound`, `m1-cpufreq`, `m1-smp`, `m1-quiet-boot`, `m1-dcp`, `m1-bwfm`, `m1-rust-kext/build-agx-kc`,
  `m1-avd`, `m1-storage`, `m1-native-boot` and its `reboot/`) now begins with one line that returns
  the flat recipe at the tip; its old body is reached only with `SF_PD=pin` and still evaluates to
  the pin's derivation (`rjlxj88c….drv` → `z66w83kk…`). Retired at the tip, kept for the pin.
- RAM root: `tools/pcie_assemble.py` takes `artifacts/rebase/ramdisk-tip.img` and the base
  `mapping.json`, whose libSystem is `/nix/store/rdi193a97w1mjkpzzcalh28nb4spp78f-puredarwin-libsystem-default-0.1`
  (`libsystem-default/build-libsystem.nix` at the tip). `chonkstep-darwin/stage_ramdisk.py`
  accepts the tip's and the pin's base image by hash.
- Launcher base tree (`runs/m1-desktop-010/pcie-m2-host-001/lab`): member gate without
  `PDSun50iMMC` (a collection that has it still passes), challenge typed at a silent console in
  every boot; record and way back: `rebase/launcher-base-change-20261112.patch`. Regenerated
  against it and verified to give the same launcher as before plus that change: `quiet-boot.patch`,
  `launcher-fast-quiet.patch` (now without the silent-console hunks: 99 → 60 changed lines),
  `launcher-native.patch` (197 → 158), `reboot/launcher-self-reboot.diff`, `m1-smp-audit/host/vcpu-dump.patch`,
  `no-video-mirror.patch`, `m1-apcie-port-packet/…/skip-port-framebuffer.patch`.
- User space staged from Nix by the wrappers (Mesa, audio server and frameworks, `pdplay`,
  `pdtbtest`, `pd-netd`, the AVD player and FFmpeg) is **still the pin's builds**: each wrapper says
  `export SF_PD=pin` for those lookups. They run on the tip's kernel and libSystem (proven);
  building them at the tip is open.

**Booting the pin:** `GPU_DESKTOP_PIN=1 tools/gpu_desktop.sh <name> <minutes>`; for other wrappers
`SF_KC=pin PCIE_PIN=1 <wrapper>` (collection `z66w83kk…`, the pin's RAM root
`artifacts/m1-bringup-009/ramdisk.img`, the mapping as it was: `mapping.json.before-tip-20261112`).
Building anything of the pin: `SF_PD=pin nix-build …`.

**Files changed outside git, with backups**
- `runs/m1-desktop-010/pcie-m2-host-001/lab/integrations/puredarwin/m1-bringup/host_progress.py`
  (backup `host_progress.py.before-tip-20261112` beside it)
- `artifacts/m1-desktop-010/apcie-port-candidate-003/mapping.json` (backup
  `mapping.json.before-tip-20261112`)
- new: `artifacts/rebase/ramdisk-tip.img`, `.json`, `.log`

**Proof boots through the default entry points:** all returned to verified Linux.

| Boot | Entry point, as it now defaults | Result |
|---|---|---|
| `sw01` | `tools/gpu_desktop.sh sw01 2` (fast quiet boot) | desktop on GL/KMS, Wi-Fi joined, ping, clean end |
| `sw02` | `GPU_DESKTOP_VERBOSE=1 tools/gpu_desktop.sh sw02 2` (text boot, kernel waits, no launcher patch) | the same; the silent-console wait is in the base tree now |
| `ds10` | `DS_SECONDS=235 m1-desktop-scale/tools/ds_desktop_boot.sh ds10 600` | all 44 autopilot lines identical to `ds09` |
| `tb12` | `m1-touchbar/tools/tb_boot.sh tb12` | 37 of 37 checks PASS |
| `sd50` | `SD_GUEST=…/darwin-speaker-test.in m1-sound/tools/sd_boot.sh sd50` | `hold_probe 0 -> 1`, tone sensed, fault 0, end marker |
| `sm36` | `m1-smp/tools/smp_boot.sh sm36` | 8 processors, par8 438.8 MB/s, begin and end markers |
| `av22` | `m1-avd/tools/avd_boot.sh av22 runs/avd/rec/bbb-360p-h264-5s.pdaj` (the wrapper's `pdavd=2`) | 150 of 150 frames equal to the software decode (`avd_compare.py`, rc 0): PDAppleAVD in the default collection |
| `nb21` | `NB_WORDS=vdm-serial NB_MODE=native NB_RETURN=vdm NB_CPUS=8 m1-native-boot/tools/nb_boot.sh nb21 175` (collection `default_kc.sh native`) | native, no hypervisor: tip kernel, `hw.activecpu = 8`, command line observed, one reset back to Linux |
| `sw03` | `GPU_DESKTOP_PIN=1 tools/gpu_desktop.sh sw03 2` | the pin's collection and RAM root through the new launcher tree: desktop, Wi-Fi, clean end |

`nb19` (tip) and `nb20` (the pin's native collection, as a control) were native boots without
`NB_WORDS=vdm-serial`: the kernel's console then never reaches the controller, both recorded 0
bytes and `NO_COMMAND_LINE_OBSERVED`, both returned to Linux on the one reset. An operator error of
this stream, not a result about either kernel; `nb21` is the same boot with the word.

`PCIE_TIMEWAIT` (used by `tb_boot.sh`, `sd_boot.sh`): `TIME_WAIT_PATCH applied=false, signature not
found` on the tip kernel; the boots ran with the kernel's remaining 30 s wait. Those wrappers
should pass `pd_quiet_wait=0 pd_rtc_wait=0` instead (not changed here).

**Staged user space at the tip (built, not yet staged):** `pdplay`, `pdtbtest`, `pd-netd` and the
AVD user tools build at the tip. `chonkstep-darwin/build-pdgop.nix` does not (`PDGOP.c:87: use of
undeclared identifier 'CFDictionaryRef'`), and with it the Mesa test bundle; the audio server does
not (the coreaudiod patch, see the follow-up section). Until those three are ported the wrappers
keep `export SF_PD=pin` for what they stage.

**QEMU default** (`tools/qemu_desktop.sh`, `qemu-dev/`): the tip, `SF_PD=pin` for the pin (same
derivations as before: `build-image.nix` `j8l87ddd….drv`, `build-kc.nix` `s9kh6cna….drv`); answer
cache and templates are kept per source. Runs `runs/qemu-dev/sw01` (tip, long sequence: ok) and
`sw02` (`SF_PD=pin`: ok). The persistent install's `wayland` image is 100 derivations at the tip
and stays on `SF_PD=pin tools/qemu_install.sh create --variant wayland`.
The `type` difference is explained and is not a tip matter: the autopilot arms when one window has
focus and measures the window focused at the start of `type`; when the second start-up terminal
maps during the phase it takes the keys and the measured window shows no change (every failing run
has `windows=1` at the settle check, every passing one `windows=2`; the typed text is on screen in
the other terminal, `rq03/shots/t001.png`; pin runs of 11 November show the same one-window state).
Fix belongs in the compositor's autopilot; sequences with `drag` before `type` are unaffected.
Correction to the QEMU section below: the `pdpowerd` line also appears on the pin.

**Cocoa patches at the tip** (checked in recipe order, not ported): apply: 0002, 0003, 0008, 0009,
0013, 0014, 0015, 0016; already upstream: 0001, 0007, and 0010 except its one `CMakeLists.txt`
hunk; fail: 0004 (`message.h`, `rpc_wayland.c`), 0005 (`NSWindow.m`, `NSApplication.m`,
`NSDisplay.m`), 0006 (`O2Context_builtin.m`), 0011 (`NSModernAppKit.h`; passes with `patch` fuzz),
0012 (`NSButtonCell.m`, `NSSegmentedCell.m`), 0017 (`NSApplication.m`), 0018 (`message.h`,
`rpc_wayland.c`). The Cocoa and Homebrew recipes still name the pin themselves.

**Not switched, and why**
- Staged user space (above): three recipes do not build at the tip.
- `tools/agx_boot.sh`, `tools/dcp_boot.sh`, `m1-sound/aop/tools/mic_boot.sh`, the non-`PCIE_SKIP_BUILD`
  paths of `tools/pcie_iterate.sh` (`m1-apcie-host/build-kc.nix`, `puredarwin-patches/`): bring-up
  tools built on the chain's lower recipes; they work with `SF_PD=pin` and are not delegated.
- `bs_boot.sh BS_KC=stock` still names a pin collection (it is that script's "before" kernel).
- `m1-smp/kernel-patches/0002`, the register-name block and the T8010 override are no longer used
  by the default build; they are kept because `SF_PD=pin` still builds from them.
- DEBUG kernel at the tip: recipes whose own default is `kernelConfig = "debug"` (`m1-cpufreq`,
  `m1-smp/build-kc-desktop`, `m1-quiet-boot`) would build one; not built or booted.
- Storage and reboot collections: built, not booted (by instruction).

**Overnight:** `integrations/puredarwin/rebase/tools/rb_overnight_smp.sh 30 20` (20 boots of the
default collection on 8 CPUs, names `rb30`..`rb49`, stops at the first boot not back in Linux).

## Tip leftovers (12 November, second stream; hand-over state, kept current)
Owner: "I want the things you haven't switched taken care of as well." Three agents: the lead
(staged user space, wrappers, bring-up tools, clean-up), a fork for the audio server
(`docs/roadmap/issues/SF-I-sound.md`) and a fork for Homebrew and the QEMU install (their docs).

**One place for staged user space: `tools/staged.sh <piece>`** (as `tools/default_kc.sh` is for the
collection). Wrappers no longer `export SF_PD=pin`; they ask it for `mesa`, `mesa-wl`, `dcp-flip`,
`drm-storm`, `sysaudio`, `pdplay`, `pdtbtest`, `netd`, `avd-user`, `pdvideo`, `compositor`,
`terminal` (or `nix <dir> <recipe>`). Every piece is the tip's build except the list `PIN_ONLY`
in that script (`sysaudio`, `pdplay`, until the audio fork reports them proven).
Switch back: `SF_STAGED=pin` (everything as staged before the rebase: pin builds, compositor
build-009, terminal build-008); `SF_STAGED_PIN="<piece> ..."` for single pieces.
`GPU_DESKTOP_PIN=1` sets `SF_STAGED=pin` too. `tools/pcie_assemble.py` takes pd-netd from
`tools/staged.sh netd` (with `PCIE_PIN=1`: the pin's build) instead of the shared link
`runs/m1-desktop-010/pd-netd-build-001/result`.

**What was wrong with `build-pdgop.nix`:** upstream commit `f8715cf9` removed PDGOP.c's
`#ifdef _PD_IOKITLIB_H` branch (string-based matching of the static `libIOKit.a`) and kept only
the CoreFoundation form, which needs IOKit.framework. `m1-wayland/pdgop-string-iokit-tip.patch`
(tip only, applied before `pdgop-fail-closed.patch`) restores the branch; the patched file is
byte-identical to the pin's `PDGOP.c`, so the host test of the fail-closed patch still covers it.
`SF_PD=pin` gives the old derivation (`nqi47vjc…`). Tip: `/nix/store/jja2jds38f6y1s5kgky7nb5gr84v0s8z-…`.

**Built at the tip** (all with the tip SDK, toolchain and `libsystem-default`):
| Piece | Tip build | Pin build |
|---|---|---|
| PDGOP + static IOKit | `jja2jds3…-puredarwin-chonkstep-pdgop-static-0.1` | `nqi47vjc…` |
| Mesa/AGX bundle (EGL, GLES, gallium, libdrm, tests) | `66jlbv4l…-darwin-lab-agx-offscreen-test-arm64-darwin` (the four libraries: 33 042 942 bytes, as the pin's) | `1wribqgj…` |
| compositor | `runs/m1-desktop-010/wm-darwin-tip-desktop-build-001` (source: a copy of build-009's; 32 251 574 bytes; the same 217 imports, libSystem only) | `wm-darwin-scale-desktop-build-009` (32 251 798) |
| terminal | `runs/m1-desktop-010/terminal-application-tip-build-001` (source: build-008's; 2 161 808 bytes, the same 148 imports) | `terminal-application-build-008` |
| pdtbtest | `vls38qpq…-darwin-lab-pdtbtest-arm64` | |
| pd-netd, pd-dhcp, pd-net-up | `9i4xfdbp…-darwin-lab-pd-netd-arm64` | `2ka564cg…` |

Recipe for the two Rust binaries: `chonkstep-darwin/tools/build_desktop_binaries.sh
compositor|terminal <name> [source-dir]` (copies the source into the build directory; link inputs
from `chonkstep-darwin/link-inputs.nix` through `darwin-linker-inputs.py`; `darwin-linker.py` with
its literal pin paths is unchanged and `SF_PD=pin nix-build link-inputs.nix` names exactly those
paths). No source change was needed: no worktree, no branch, nothing in the compositor repository.

**Fast-boot arguments:** `tb_boot.sh`, `mic_boot.sh`, `la_hangrate.sh` pass
`pd_quiet_wait=0 pd_rtc_wait=0` instead of `PCIE_TIMEWAIT` (`TB_WAITS=stock`/`MIC_WAITS=stock`: the
kernel's 30 s registry wait as before). The sound wrappers are the audio fork's. `nv_boot.sh`
(storage stream) still passes `PCIE_TIMEWAIT`, which does nothing at the tip.

**Bring-up tools at the tip** (recipes; boots listed below when run):
- `tools/pcie_iterate.sh` without `PCIE_SKIP_BUILD`: at the tip builds `rebase/build-kc.nix` and
  pd-netd from the lab directories into the shared links; `SF_PD=pin`: the pin's chain as before.
- `tools/pcie_export_patch.sh`: base revision follows `SF_PD` (list files read from the commit);
  `puredarwin-patches/0001`, `0002` regenerated against `c713d8fb`; `puredarwin-patches/build-kc.nix`
  at the tip is the default collection with the bridge and the Wi-Fi pair from the patch build
  (`rebase/build-kc.nix` gained `bridge` and `wifi` arguments; the default derivation is unchanged:
  `kf45zapn….drv` → `71zynndd…`).
- `m1-sound/aop/build-kc.nix` (for `mic_boot.sh`): at the tip the default collection plus PDAopAudio.
- `bs_boot.sh BS_KC=stock`: at the tip the default collection with a kernel without
  `m1-boot-speed/kernel-patches/0001` (`rebase/kernel.nix { bootSpeed = false; }`).

## State (before the switch)
| Step | State |
|---|---|
| Source switch applied to the shared recipes | **done** (commit `510737cc2`): 76 recipes import `rebase/pd.nix`; all 79 recipes that named the pin instantiate to the same derivation before and after, as do the default collection (`rjlxj88c….drv` → `z66w83kk…`) and the Nix-built RAM root inputs. Three recipes keep the revision because their own text is a build input (`m1-keyboard/build-kext.nix`, `m1-network-default-route/build.nix`, `m1-apcie-bridge/root-build/build-private-policy-objects-001.nix`); found by the comparison, which first showed 6 differing derivations |
| Kernel at the tip (`rebase/kernel.nix`, DEVELOPMENT) | **builds**: `/nix/store/2g19j25q7c4hsa4lamvh4ffrjwj2460c-puredarwin-kernel-arm64-t8103-siliconforest-0.1`. The two ported patches compile |
| Collection at the tip (`rebase/build-kc.nix`) | **builds, with upstream's kext link rule** (`-text_exec`): `/nix/store/8yk7f7qnp7mcsm40a291ja1wh8dwyw5j-puredarwin-kc-arm64-0.1`, 34 277 246 bytes (pin: 33 851 054). Every kext compiled unchanged, the Rust GPU driver included. `kc_overlap.py`: 0 overlapping segments. Members: the pin's 60 minus `PDSun50iMMC` |
| Launcher's offline load check | **passes** with `rebase/launcher-tip-members.patch` (59 members) |
| libSystem at the tip (`rebase/build-libsystem.nix`, the three patches of `libsystem-default`) | **builds**: `/nix/store/69sdpjfj7aypp318yqldys9a7b7a60w8-puredarwin-libsystem-siliconforest-0.1` |
| Tip minimal image and RAM root | **built**: image `/nix/store/ggmrin8nc318cnmd6mrw9r1viggswg28-puredarwin-image-0.1`; RAM root base `artifacts/rebase/ramdisk-tip.img` (375 529 472 bytes, sha256 `bd6b06c1…`; pin: 373 284 864), same partition offsets as the pin's image. The slim stager makes a 384 MiB RAM root from it unchanged |
| QEMU | **desktop reached, autopilot results equal to the pin's** in two of three sequences; one timing difference, see "QEMU" |
| M1 | **twelve boots `rb01`..`rb12`, every area passes** against its pin reference; one launcher-proof failure (`rb03`) explained and avoided since; see "M1 boots" and "Equivalence" |

## QEMU (12 November)
Tip runs: `rebase/tools/rb_qemu.sh` (the QEMU stream's `tools/qemu_desktop.sh`, `qd_image.py`
and `qd_run.py` unmodified, output root `runs/qemu-dev-rebase`; base image `rebase/qemu-image.nix`
= `/nix/store/8a0dain89f8ga6kp73ldb2kpmd4mfl45-puredarwin-image-0.1`, upstream's RELEASE virt
kernel and kexts at the tip with that stream's three patches; the tip's libSystem staged; the
M1's compositor build-009, terminal build-008 and the pin-built audio server, as on the pin).
Pin runs of the same commands on the same night: `tools/qemu_desktop.sh --name rbpin<n>`.

| Sequence (`--only`) | Pin | Tip |
|---|---|---|
| settle, drag, type, fullscreen, scrollback, resize, redrag, minimize, menu, settings, chords, menuterm, limit | `rbpin1`: every phase as the tip; verdict FAILED because **resize is NOT-RESIZED with build-009 on the pin too** (not a rebase matter) | `rq01`: the same 22 result lines, phase for phase |
| the same without resize and redrag | `rbpin2`: ok | `rq02`: ok, same result lines |
| settle, type, clipboard | `rbpin3`..`rbpin6`: ok, 4 of 4 | `rq03`..`rq06`: clipboard PASS 4 of 4; **type NO-CHANGE in 2 of 4** (`rq03`, `rq04`), ok in `rq05`, `rq06` |

The one difference: in the short sequence the `type` phase (the first after a 2 s settle) twice
saw no pixel change in the terminal at the tip. In both runs the screen after `settle` has a hash
(`3f2cf481…`) that no passing run has, and the screen after the later phases is the same as the
pin's (`rq04` after `type`, all four after `clipboard`): the desktop was not yet in its usual
state when the 2 s settle ended, and it converged. In the long sequences (`rq01`, `rq02`), where
`drag` comes first, `type` passed. Not explained further; it is a start-up timing difference of
the tip's user space in QEMU and is listed under "Regressed or not proven".

Also seen at the tip only: a line `pdpowerd: no org.puredarwin.acpi events, nothing to watch` on
the console (a daemon upstream added).

## M1 boots (12 November)
All through `rebase/tools/rb_boot.sh` → `tools/pcie_iterate.sh` (queued, `BENCH_WHO=rebase`,
`PCIE_KC` = the tip collection, `PCIE_STAGER` = the tip RAM root, no chain, never `pdnvme=`).
Every guest script starts with `rebase/guest/rb-prologue.sh`: if anything is attached to the
NVMe nub, or any object of an NVMe class exists, the session ends at once. Every boot so far
printed `RB_NVME ioreg_rc=0 entries=1 nvme_objects=0` (the nub `ans@77400000` alone, an
`IOPlatformDevice` with no child) and returned to verified Linux.

| Boot | What | Result | Pin reference |
|---|---|---|---|
| `rb01` | shell, 1 CPU, text boot; registry of our drivers' nodes | shell reached, `DEVELOPMENT_ARM64_T8103` kernel `xnu-12377.121.6`, all drivers attached (PCIe bridge, PDBwfm/en0, SPI keyboard + trackpad, PDGpuDT, PDCpuPower, sound, Touch Bar), no panic; device memory of `apcie`, `dart-apcie0`, `smc`, `ans` at the addresses the device tree gives | no pin boot of this script; drivers and messages as pin boots |
| `rb02`, `rb04`, `rb07` | 8 CPUs, SMP bench (`darwin-smp-bench.in`) | 8 of 8 processors, 4 P + 4 E; par8 435.2 / 435.6 / 438.4 MB/s, big8 594.6 MB/s each; begin and end markers, no panic | `sm33`, `sm34`: par8 435.2 / 436.8 MB/s |
| `rb03` | the same | **the launcher's console proof failed** (`NO_COMMAND_LINE_OBSERVED`): the system was up at the shell on 8 CPUs, but the Wi-Fi driver's lines landed inside the answer to the launcher's challenge. Cause and fix below. Linux verified afterwards | none |
| `rb05` | Wi-Fi: scan, join, DHCP, ping gateway and internet, HTTP, DNS (`darwin-wifi-join-test.in`), 8 CPUs | every return code 0, 0 % loss | as the join/DHCP reports |
| `rb06` | GPU + display + compositor autopilot, both sessions, as `ds09` (235 s, quiet boot with the kernel's waits) | all 44 autopilot result lines identical to `ds09` (24 verdicts, both `done result=ok`); GL on KMS 2560x1600, dmabuf v3; mean 10.1 fps over the phases, 7 missed refreshes | `ds09`: 10.2 fps, 4 missed |
| `rb08` | sound, headphone path (`darwin-sound-test.in`) | the same 55 result lines as the pin, 7 phases, PLL locked at 48 and 44.1 kHz, late/ring/DART/I2C errors 0, end marker | `sd05` |
| `rb09` | speakers, `kern.pd_speaker.hold_probe=1` (`darwin-speaker-test.in`) | hold set, tone measured by the sense amplifiers on all four, model in charge, fault 0, end marker | `sd14` (which monitored a longer tone: 100 lines to 85) |
| `rb10` | Touch Bar (`darwin-touchbar-test.in`) | 37 of 37 checks PASS, the same list as the pin | `tb10`: 37 of 37 |
| `rb11` | boot timeline (`darwin-gpu-desktop-fast.in`, quiet fast boot, slim RAM root) | **first frame 6.44 s** after the hypervisor starts the guest (bound 7.5 s); kernel start to root 0.89 s; 48.2 s after the cycle starts | `df02`: 7.27 s and 51.2 s |
| `rb12` | the whole desktop as `tools/gpu_desktop.sh` stages it, 120 s, nobody at the machine | Wi-Fi joined, network up, coreaudiod, Touch Bar ready, keyboard and trackpad nodes, GL on KMS, session ended cleanly (`cleanup=Complete`) | `df02` |

Twelve boots, twelve returns to verified Linux, no panic line in any console.

**`rb03` and the launcher's challenge.** At the tip a text boot mounts its root 32 s after the first
console byte instead of 62 s: upstream's platform expert now publishes `IORTC` on this machine, so
the kernel's 30 s wait for a clock is gone (the 30 s registry wait is still there:
`IOKitInitializeTime: registry not quiet after 30 s`). On the pin the Wi-Fi driver finishes 0.4 s
before the root mounts; at the tip it finishes 0.5 to 1 s after, when the launcher is already
typing its challenge at the prompt, and a kernel line inside the answer fails the proof. That is
the situation `m1-boot-speed/launcher-fast-quiet.patch` was written for. `rb01`..`rb04` ran with
only the member patch (3 of 4 got through); from `rb05` on, text boots carry
`rebase/launcher-tip-text.patch` (member patch + the fast-quiet patch's "type at a silent console"
hunks, nothing else) and the stock quiet boot `launcher-tip-quiet.patch` (the same on
`quiet-boot.patch`): 8 of 8 since.

**8 CPUs and the corecrypto hang.** Three good boots do not show that the hang the DRBG patch fixed
(1 boot in 9 without the patch) is absent at the tip; the patched kext is in the collection and
upstream's corecrypto changed one unrelated header. `rebase/tools/rb_overnight_smp.sh <first
number> 20` is the longer run, for the overnight batch; it has not been run.

## Equivalence, pin against tip
| Suite | Pin | Tip | Equal |
|---|---|---|---|
| Collection builds, fits, passes the launcher's offline check | `z66w83kk…`, 60 members | `8yk7f7qn…`, 59 members (no `PDSun50iMMC`), upstream's `-text_exec` link rule | yes, with the launcher member patch |
| QEMU desktop, long autopilot sequences | `rbpin1`, `rbpin2` | `rq01`, `rq02` | yes |
| QEMU desktop, `settle,type,clipboard` | 4 of 4 | clipboard 4 of 4, type 2 of 4 | **no** (start-up timing, see "QEMU") |
| Shell on 1 CPU | (earlier boots) | `rb01` | yes |
| 8 CPUs, throughput | `sm33`, `sm34` | `rb02`, `rb04`, `rb07` | yes; hang not re-proven (overnight run) |
| Wi-Fi scan, join, DHCP, ping, DNS | join reports | `rb05` | yes |
| GPU, display, compositor autopilot (20 phases, 2 sessions) | `ds09` | `rb06` | yes, line for line |
| Sound, headphones | `sd05` | `rb08` | yes (the kernel driver and `pdplay`; the audio server is the pin's build) |
| Sound, speakers held | `sd14` | `rb09` | yes |
| Touch Bar | `tb10` | `rb10` | yes |
| First frame after guest start | 7.27 s (`df02`) | 6.44 s (`rb11`) | yes, faster |
| Whole desktop | `df02` | `rb12` | yes (unattended; nobody has looked at the panel or listened) |
| Read-only NVMe | not booted by this stream | not booted | n/a |

## Regressed or not proven
- **QEMU `type` after a 2 s settle** fails intermittently at the tip (2 of 4), never on the pin
  (0 of 4). The end states agree. Not explained.
- **Text and stock-quiet boots need the launcher to type at a silent console** at the tip (a
  consequence of upstream removing the clock wait). Without it about one text boot in four fails
  the console proof (`rb03`). Every suite script that still uses `quiet-boot.patch` or no launcher
  patch (`ds_desktop_boot.sh`, `tb_*`, `sd_*`, `smp_boot.sh`) is affected once the tip is default.
- **`PCIE_TIMEWAIT`** (the instruction-pattern patch) was not tried on the tip kernel; the tip
  needs it less (one 30 s wait instead of two) and the default desktop uses boot-args.
- **The audio server and frameworks, Mesa, compositor, terminal and Wi-Fi tools were not rebuilt**
  at the tip: the pin's binaries ran on the tip's kernel and libSystem in every boot above. No
  rebuild of the compositor was needed. Whether they *build* against the tip's SDK is not shown
  (21 leaf recipes evaluate; only the kexts, kernel, libSystem, ioreg and the images were built).
- **`/arm-io/ans`** is published as a nub at the tip (`ans@77400000`, `IOPlatformDevice`,
  registered, no child, no NVMe object, no disk node) in all twelve boots. Nothing drives it; it
  is new surface for any NVMe kext added later.
- **Console:** 24 `PD-IOK` lines per boot from upstream, and `cs_invalid_page … allowing` lines
  for the staged `ioreg` (the pin prints the same kind for other unsigned binaries).
- The 20-boot 8-CPU run, a person at the panel, and listening tests: not done.
- `puredarwin-patches/` (the generated exports) and the Cocoa stream's patches 0004..0006 are
  not ported.

## Default switch (for the owner)
1. `integrations/puredarwin/rebase/default-changes.patch` (applies to `tools/gpu_desktop.sh`):
   the tip collection `/nix/store/8yk7f7qnp7mcsm40a291ja1wh8dwyw5j-puredarwin-kc-arm64-0.1` and
   `rebase/tools/stage_ramdisk_rebase.py` become the defaults, the launcher patch becomes
   `rebase/launcher-tip-fast-quiet.patch` (text boot: `launcher-tip-text.patch`);
   `GPU_DESKTOP_PIN=1` boots the pin as today.
2. In the launcher tree (`runs/m1-desktop-010/pcie-m2-host-001/lab`, not in git): apply
   `rebase/launcher-tip-text.patch` once. Then the member gate no longer names `PDSun50iMMC`,
   the challenge is typed at a silent console in every mode, and the three `launcher-tip-*`
   patches are replaced by the originals (`quiet-boot.patch`, `launcher-fast-quiet.patch` must be
   regenerated against that tree: both touch the lines the text patch changes). A pin collection
   then needs the old tree or a patch putting the name back.
3. RAM root inputs at the tip: base `artifacts/rebase/ramdisk-tip.img` (+ `.json`), made by
   `rebase/tools/rb_ramdisk_base.sh` from `/nix/store/ggmrin8nc318cnmd6mrw9r1viggswg28-puredarwin-image-0.1`;
   libSystem `/nix/store/69sdpjfj7aypp318yqldys9a7b7a60w8-puredarwin-libsystem-siliconforest-0.1`
   (`rebase/build-libsystem.nix`); every other staged file as on the pin. To make that the
   assembler's own default: `ORIGINAL` in `tools/pcie_assemble.py`, `BASE_SHA` in
   `chonkstep-darwin/stage_ramdisk.py`, and the libSystem entry of
   `artifacts/m1-desktop-010/apcie-port-candidate-003/mapping.json` (source and `replaces_sha256`
   `29074679…`).
4. The suite scripts' `*_KC` defaults (`ds`, `tb`, `sd`, `smp`, `bs`), `nb_boot.sh` and
   `ls_desktop_boot.sh` name pin collections; until they are changed, `rebase/tools/rb_boot.sh`
   runs any guest template against the tip.
5. `SF_PD=tip` stays the way the tip is built; flipping the default inside `rebase/pd.nix` would
   send the pin's fifteen-recipe collection chain to the tip, where it does not evaluate (it names
   pin store paths). That chain can be retired, not switched.
6. QEMU: `rebase/tools/rb_qemu.sh`; for the QEMU stream's own default, `build-image.nix` and
   `build-kc.nix` in `qemu-dev/` take `(import ../rebase/pd.nix).pd` and `PD_REV` in `qd_image.py`
   follows.


## The switch (deliverable 2)
`rebase/pd.nix` holds both sources (checkout + revision) and returns the flake for `SF_PD`
(unset or `pin`: the pin, with the very flake reference the recipes carry today; `tip`: the tip).
`convert_recipes.py --write` replaces the `builtins.getFlake "…rev=e7aa…"` expression in 79 live
recipes by `(import <path>/rebase/pd.nix).pd` and changes nothing else. It leaves alone: frozen
history (`revisions/`, `attempts/`, `implementation-revisions/`, `host-revision*/`,
`build-recipes/`) and the other streams' directories (`cocoa`, `qemu-dev`, `homebrew`,
`libsystem-default`, `m1-native-boot`), which keep naming the pin (12 recipes).

**Checked on a scratch copy of `integrations/puredarwin` (the real recipes are not converted
yet):** 15 entry points instantiate to the same derivation before and after, among them the
default collection (`m1-boot-speed/build-kc.nix` → `rjlxj88c…-puredarwin-kc-arm64-0.1.drv` →
`/nix/store/z66w83kk8gj61xv5ych34rz1zv18hk70-puredarwin-kc-arm64-0.1`) and every Nix-built file
`tools/gpu_desktop.sh` stages (`m1-mesa-asahi/agx-offscreen-test.nix`,
`m1-sound/sysaudio/build-sysaudio.nix -A all`, `m1-sound/build-pdplay.nix`,
`m1-bwfm/netd/build-pd-netd.nix`, `m1-userspace-diagnostics/build-libsystem-spawnattr.nix`), plus
`m1-smp/build-kc-desktop.nix`, `puredarwin-patches/build-kc.nix`, `m1-touchbar/build-pdtbtest.nix`,
`m1-storage/build-kext.nix`, `m1-dcp/dcp-flip-test.nix`, `m1-mesa-asahi/agx-wl-demo.nix`,
`m1-network-native-resolver/build-mdnsd.nix`, `chonkstep-darwin/build-pdgop.nix`,
`chonkstep-darwin/target-deps.nix`. Not yet compared one by one: the other converted recipes
(older collections and probes); that comparison runs when the conversion is applied, and a
difference stops the work.

With `SF_PD=tip` on the same scratch copy, 21 leaf recipes evaluate (all kexts of the default
collection, the patched platform expert, corecrypto, IOHIDFamily and framebuffer builds,
kc-tools, libSystem with the spawn fix, the audio server, pdplay, pd-netd, Mesa, pdtbtest, the
NVMe kext). Evaluation only: whether they compile at the tip is the next step.

Two things the switch must not do, and how that is kept:
- **Caches keyed on recipe text.** `m1-boot-speed/tools/bs_nix_path.py` (used by
  `gpu_desktop.sh` and the suite scripts) and `qemu-dev/qd_image.py`
  (`runs/qemu-dev/base/inputs.json`) remember store paths by recipe content, not by environment.
  A tip run through them would either get pin paths or leave tip paths for the next pin run.
  Rule: `SF_PD` is set only by the rebase stream's own wrappers, always with `BS_NIX_CACHE=0`
  (never reads or writes that cache), and never for `qd_image.py`. With the conversion,
  `bs_nix_path.py` also gets `SF_PD` added to its key when the variable is set (pin keys unchanged).
- **The pin collection chain is not made two-headed.** The pin's collection is 15 recipes that
  rewrite each other's `buildPhase` text and name six store paths outright (kernel, kernel source,
  platform kext, bridge, two keyboard bundles). The tip gets flat recipes under `rebase/`
  (`kernel.nix`, then `build-kc.nix`, `ramdisk`, `qemu`) that call the same converted leaf recipes
  and list members in the pin collection's order; the chain itself is only read.

After the conversion every `bs_nix_path.py` key changes once (recipe text is part of the key), so
each tool's first run re-evaluates (20 to 70 s per recipe) and gets the same paths as before.

## What the working-tree changes in the three checkouts are
`sources/puredarwin-m1-desktop` (21 files), `-m1-bringup` (19), `-baseline` (clean):
- 14 files under `src/Kernel/xnu`: the kernel half of `m1-bringup/candidate.patch`, and in
  `-m1-desktop` also `m1-display/console-resume.patch` (`video_console.c`, `pe_init.c`). Exactly
  this is the source snapshot every pin collection's kernel is built from
  (`/nix/store/fzwhq5qb…-source` in `m1-dcp/kernel.nix`, `/nix/store/274mmx79…-source` in
  `m1-display/console-return/build-kc.nix`: both compared file by file, identical to the
  `-m1-desktop` tree and different from the pin in these 14 files only).
- 5 files under `src/Kernel/Extensions/PDArmPlatformExpert`: exactly
  `m1-bringup-platform/platform.patch` applied (compared file by file). The builds do not read
  them: the recipes apply the patch file to the pinned source.
- `flake.nix`, `nix/m1-bringup.nix`: the `kernel-arm64-t8103-bringup` package the first kernels
  were built with. Not used by current builds (`m1-dcp/kernel.nix` overrides the flake's T8010
  kernel instead).

The checkouts are therefore not needed for the tip: the kernel delta is now a patch file
(`m1-bringup/kernel-patches-tip/0001`), and upstream has a T8103 kernel package to start from.

## Inventory: every patch and overlay on the PureDarwin tree, at the tip
Method: each series applied in recipe order with `git apply` to a worktree of the tip (and, as a
control, of the pin, where all of them apply). "Applies" is textual; nothing here is compiled yet.

### Kernel (xnu), in build order
| Patch | At the tip |
|---|---|
| bring-up base (working tree of `-m1-desktop`: `candidate.patch` kernel half + `console-resume.patch`) | **conflicts with upstream's T8103** in 3 of 14 files (`MakeInc.def.in`, `MASTER.arm64.t8103`, `board_config.h`) and overlaps in `caches_asm.s`; **ported**: `m1-bringup/kernel-patches-tip/0001` (merge decisions below) |
| `m1-dcp/kernel-patches/0001` pd_lfile sendable, `0002` select/poll/kevent, `0003` mmap | apply (in series; `0003` needs `0002`) |
| `m1-dcp/kernel-patches/0004` T8103 as DEVELOPMENT | applies on the ported base |
| `m1-smp/kernel-patches/0001` AppleARMSMP reset vector | applies |
| `m1-smp/kernel-patches/0002` cpu_init MIDR line uses kprintf | **upstream has it** (same change in `cpu.c`): dropped at the tip |
| `m1-smp/kernel-patches/0003` park offline cores, idle accounting, deep idle | one hunk (`board_config.h`) needed moving; **ported**: `m1-smp/kernel-patches-tip/0003` |
| `m1-quiet-boot/kernel-patches/0001`, `0002` | apply |
| `m1-boot-speed/kernel-patches/0001` registry-quiet wait | applies (same file upstream added log lines to) |

Merge with upstream's T8103 (commit `fda0135e`, made for darwin-vm's QEMU machine: EL2/VHE entry,
no AppleARMSMP, `PUREDARWIN_NO_KERNEL_INTEGRITY`):
- **Upstream's taken, ours dropped:** `T8103` in the supported-configuration and large-memory
  lists; the assembler register names `HID4`, `EHID4`, `CPU_OVRD`, `CPU_CFG`, `ACC_OVRD`,
  `ARM64_REG_HID4_DisDcMVAOps` (now in `apple_arm64_regs.h`, same encodings); upstream's EL2 entry
  in `start.s` stays (not taken at EL1 under the m1n1 hypervisor).
- **Ours kept:** the machine flags (`DARWIN_LAB_M1_BRINGUP`, not
  `PUREDARWIN_NO_KERNEL_INTEGRITY`: that one turns `USE_APPLEARMSMP` off, and the lab starts its
  cores through AppleARMSMP + `PDAppleBootCPUPMGR`); our `MASTER.arm64.t8103` (derived from
  `MASTER.arm64.MacOSX`; upstream's is an xsmall/embedded base without SysV IPC: a different kernel
  for user space); our cache helpers (`m1_cache_helpers.inc`), with upstream's four functions of the
  same names compiled out under our flag. Ours restores the HID4 value it found, masks interrupts
  while that value is live and returns on an empty range; upstream's
  `CleanPoC_DcacheRegion_Force_nopreempt` switches the VA operations off unconditionally when it
  returns, also inside a caller's batch.
- **Could be dropped later, after a test of its own:** the cache helpers (if upstream's are made
  nesting-safe or shown not to nest), `m1_progress_entry.inc` and the `M1_PROGRESS` breadcrumbs
  (bring-up aids).

### PDArmPlatformExpert
| Patch | At the tip |
|---|---|
| `m1-bringup-platform/platform.patch` | applies |
| `m1-smp/platform-patches/0001`..`0004` | apply |

Upstream changed the same kext (+514 lines). What reaches the M1: `publishSocNubs()` now also
publishes `/arm-io/ans` on Apple SoCs (see Risks 3); the model name comes from the device tree
(`hw.model` changes); PSCI is tried by SMC on an Apple SoC and cores are started directly through
PMGR only at EL2. None of upstream's CPU start code is equivalent to ours under the hypervisor
(EL1, AppleARMSMP), so nothing is dropped.

### corecrypto, libSystem, other libraries
| Patch | At the tip |
|---|---|
| `m1-smp/corecrypto-patches/0001` DRBG state size | applies to corecrypto `f577aafe`; **upstream has not fixed it** (the submodule changed one file, `cc_config.h`) |
| `m1-userspace-diagnostics/fix-spawnattr/posix-spawnattr-init-zero.patch` (the RAM root's libSystem) | applies; **not fixed upstream** (`posix_spawn.c` unchanged) |
| `m1-userspace-diagnostics/fix-root/vm-page-size-symbol.patch` | applies (not in the current RAM root) |
| `libsystem-default/patches/macos-abi.patch` (was `homebrew/patches/libsystem-homebrew.patch`; issue #46, stack probe) | applies; still needed (Homebrew stream's file, not changed here) |
| `homebrew/patches/security-copy-certificate-chain.patch` | applies (Homebrew stream) |
| `m1-wayland/pdgop-fail-closed.patch` (PDGOP, linked into the compositor) | applies |
| `m1-sound/sysaudio/coreaudiod-dev-backend.patch` | **fails, all 10 hunks**: upstream rewrote `coreaudiod.c` (1079 → 1721 lines: capture, input IOProcs, several devices). Needs a real port |

### Kexts built from the tree with a patch
| Patch | At the tip |
|---|---|
| `m1-display/framebuffer-30bit.patch`, `m1-display/console-return/console-return.patch`, `m1-wayland/iogop-vram-write-combine.patch` (IOGOPFramebuffer) | apply (the kext is unchanged upstream) |
| `m1-native-input/revisions/002/candidate.patch` (IOHIDFamily; the recipe also checks file hashes before and after) | applies; IOHIDFamily is unchanged upstream, so the hashes hold |
| `m1-rust-kext/lkpi/patches-layer/0001`..`0040` (Linux layer, `src/Kernel/fbsdkpi`) | all 40 apply |
| `m1-drm72/patches/puredarwin/0001`, `0002` | apply |
| `m1-bringup/kc-tools-arm64-text.patch` (kc-tools, a flake input) | same input revision; unchanged |

### Kexts added to the tree as directories (no patch; `cp` + `add_subdirectory`)
PDApcieT8103Bridge (+ DART pool/map, SMC), IO80211Family + PDBwfm, PDGpuDT, PDCpuPower,
PDAppleSound, PDAppleTouchBar, PDM1SPIHID (multitouch + `spihid-touchbar.patch` on our own
source), PDDcp/PDDRMDri/PDAgx (Linux layer, Rust), PDAppleNVMe (not in the default collection).
The families they build on are unchanged between pin and tip (IOPCIFamily, IO80211Family,
IONetworkingFamily, IOGraphicsFamily, IOAudioFamily, IOHIDFamily, IOStorageFamily: no diff), and
no kernel export was removed (`Unsupported.arm64.exports` +4, nothing else). All 12 recipes
evaluate at the tip. Open until built: see Risks 1.

### Exports for upstream (`puredarwin-patches/`)
`0001-m1-pcie-wifi-kexts.patch` and `0002-…userspace-and-registry.patch` fail in the list files
only (`src/Kernel/Extensions/CMakeLists.txt`, `src/Userspace/CMakeLists.txt`, `nix/lib/kexts.nix`).
They are generated (`tools/pcie_export_patch.sh`) and not on the default build path: regenerate
against the tip after the equivalence proof.

### QEMU-only (QEMU stream's files, read only)
| Patch | At the tip |
|---|---|
| `qemu-dev/pdhid-publish-keyboard.patch` | applies; **not fixed upstream** (IOHIDFamily unchanged) |
| `qemu-dev/virtio-net-name-interface.patch` | applies; **not fixed upstream** (IOVirtIONet unchanged) |
| `qemu-dev/loader-gop-mode.patch` (xnu-loader, a flake input) | same input revision |

### Cocoa (Cocoa stream's files, read only)
`0001` is in the tip already; `0002`, `0003` apply; `0004` (WindowServer), `0005` (AppKit) and
`0006` (Onyx2D) fail at the tip. That stream's port; reported to it through the owner.

### Not PureDarwin-tree patches (unaffected by the rebase as text)
Driver sources and their patches against Linux (`m1-dcp/patches`, `m1-rust-kext/agx/patches`,
`m1-rust-kext/lkpi/patches`, `m1-lkpi-extras/rust/patches`, `m1-drm72/patches/drm-kmod`), Mesa
(`m1-mesa-asahi/patches`), the compositor (`chonkstep-darwin`, `m1-touchbar/wm-darwin-*.patch`)
and the launcher patches (`quiet-boot.patch`, `launcher-fast-quiet.patch`, `host_progress-smp.patch`,
`vcpu-dump.patch`, …). The launcher has one check that the tip breaks: Risks 2.

## Did upstream fix what we found?
| Finding | Upstream at the tip |
|---|---|
| corecrypto DRBG state size overrun | no; our patch still applies and is still needed |
| `IOKitInitializeTime` waits (2 × 30 s) | partly, and not for us: `IORTC` is published at once when the platform model name is `Apple ARM`; with the device tree's model (`MacBookPro17,1` on the J293) that branch is not taken, and the registry-quiet wait is unchanged. Our boot-args patch stays |
| IOHIDFamily keyboard node (`/dev/usb_hid_kbd`) | no |
| virtio-net interface naming | no |
| `posix_spawnattr_init` leaves fields unset | no |
| cpu_init printing with interrupts off (our `m1-smp` 0002) | yes, same fix |

## What upstream changed that affects us
- **Kernel for kexts:** no export removed; public IOKit and libkern headers unchanged; our kexts
  need a rebuild, not an adaptation, as far as the source says.
- **Kext layout:** `cmake/kext.cmake` now links arm64 kexts with `-text_exec` (code in
  `__TEXT_EXEC`, as riscv did before). Every member of the collection changes shape (Risks 1).
- **Collection members:** `kc-arm64.nix` builds its member list from a table and has
  `omitKexts`. Gone: `PDSun50iMMC`. New in the base list: `PDTegraSDMMC`, `PDGadgetFamily`,
  `PDTegraUSB`, `PDTegraXUSB`, `PDTegraDisplay`, `PDFBSDem` (49 → 54 members).
- **Kernel behaviour:** about 25 new `PD-IOK …` lines on the console per boot
  (`IOStartIOKit.cpp`, `pe_init.c`); `consdebug_putc` no longer calls `PE_kputc` when it is the
  video console; `IODTResolveAddressCell` accepts ranges whose parent and child cell counts differ;
  kernel-to-user MIG replies over 1 KiB go on the heap (`-maxonstack 1024`); `setxattr` without
  doubleagentd returns `ENOTSUP` instead of `ENOATTR`; core dumps are logged in every
  configuration; a `pd,mirror` serial property and a RAM-log console exist (need device-tree
  properties we do not have).
- **Linux layer:** new `src/Kernel/fbsdkpi/xnu/pd_dri_cdev.cpp` (a `/dev/dri` clone device);
  our layer patches for DRI descriptors (0020, 0028) apply but meet new code (Risks 4).
- **libSystem and dyld for our binaries:** libSystem gained 119 exports (libproc, dirhelper) and a
  dirhelper hook; dyld gained an opt-in `DYLD_PD_MISSING_OK`. Nothing removed: the compositor
  (`wm-darwin-scale-desktop-build-009`), terminal (`terminal-application-build-008`), Mesa, the
  audio server and the Wi-Fi tools built on the pin are expected to run unchanged on the tip's
  libSystem. To be shown in QEMU first; no rebuild of the compositor is planned unless that fails.
- **Audio user space:** CoreAudio, AudioToolbox and coreaudiod were reworked upstream; our
  coreaudiod backend patch no longer applies.
- **Image and RAM root:** the RAM root is the ext4 root of the pin's `image-arm64-virt-minimal`
  (`m1-rootfs/extract-ramdisk.sh`, store path `f35jpffq…`), shrunk, plus
  `artifacts/m1-desktop-010/apcie-port-candidate-003/mapping.json` and the extras each tool adds.
  At the tip the image is 69 derivations to build (kernel, libSystem, zsh, toybox, launchd, IOKit,
  Foundation, X libraries for the userland package); partition offsets must be read again, not
  assumed. `tools/pcie_assemble.py` names the base image by path; `PCIE_STAGER` is the override.
- **Toolchain/SDK:** SDK rebuilt (MIG host changes); cross clang and ld are the same nixpkgs
  packages with new wrappers. The `clang-msl.patch` is for the guest-hosted clang, which we do not
  build.

## Risks
Outcome of each, after the builds and boots: 1 did not occur (the collection fits and boots with
upstream's link rule); 2 handled by the launcher patch; 3 checked in every boot, nothing attached;
4 and 5 did not show (GPU autopilot identical, device memory right); 6 measured (first frame
6.44 s); 7 not tried; 8 as planned; 9 avoided by the wrapper; 10 as planned. The list as written
before the builds:

1. **Kext code moves to `__TEXT_EXEC`.** The pin's collection has less than one kext of room
   below the kernel's `__TEXT` (`kc_overlap.py`), `kc-tools` carries our arm64 `__TEXT` patch, and
   the launcher's `normal_image.py` (hash-pinned, not to be edited) parses the collection's
   segments. An extra segment per kext costs alignment padding in 16 KiB pages. If the collection
   does not fit or the offline load check refuses it: build the tip's kexts with the pin's link
   rule (a one-line tree patch reverting `-text_exec` for arm64 in `cmake/kext.cmake`), report it,
   and keep the new layout as a separate item.
2. **The launcher's member gate** (`host_progress.py`, `APCIE_NATIVE_MEMBERS`) requires
   `org.puredarwin.kext.PDSun50iMMC`, which no longer exists. The tip collection cannot pass it
   unchanged. Planned: a per-boot `PCIE_LAB_PATCH` that removes that one name from the list (and is
   combined with the quiet-boot patch for the boots that use both); nothing in the reset or return
   path is touched. The five new Tegra/gadget kexts and `PDFBSDem` are left out with `omitKexts`
   (hardware we do not have, and room).
3. **`/arm-io/ans` is published as a nub** by upstream's platform expert on Apple SoCs. No kext in
   the tip matches it and our NVMe kext is not in the default collection, so nothing should attach.
   The first M1 boot checks the registry for a driver on that nub before anything else is
   concluded; `pdnvme=` is never passed.
4. **DRI descriptors:** upstream's new `/dev/dri` clone device next to our select/poll/clone-slot
   patches. Textually clean; behaviour shown only by the GPU stage.
5. **Device-tree address resolution** changed in `IODTResolveAddressCell`. The first M1 boot
   compares the `IODeviceMemory` ranges of the nodes our drivers map (PCIe, DART, SMC, DCP, AGX,
   audio, SPI) with a pin boot's.
6. **Console volume and timing:** 25 more lines before the root mounts; the quiet boot and the
   7.5 s first-frame figure are measured again, not assumed.
7. **`PCIE_TIMEWAIT`** patches the kernel by instruction pattern; a recompiled kernel may not
   match. The default desktop does not use it (boot-args `pd_quiet_wait=0 pd_rtc_wait=0`).
8. **Sound user space at the tip.** For the equivalence proof the pin's audio server and
   frameworks (already built) run on the tip's kernel and libSystem; porting our backend onto
   upstream's new coreaudiod is a separate piece of work and is reported as not done.
9. **QEMU tools share state:** `qd_image.py` deletes every older template of the same kernel and
   variant when it makes a new one, and keeps its Nix answers in one file. A tip run through
   `tools/qemu_desktop.sh` with `QD_BASE_IMAGE` would evict the QEMU stream's pin template each
   time. See "Needed from the owner".
10. **Host load:** all tip builds are new (nothing of the tip is in the store). Order below builds
    the small set first; the image (69 derivations) is built once, `--max-jobs 2 --cores 3`.

## Order of work
1. Apply the conversion; compare every converted recipe's derivation before and after; add
   `SF_PD` to the `bs_nix_path.py` key. Stop on any difference.
2. Tip toolchain, `xnu-headers-arm64`, `kexts-arm64`, the kernel (`rebase/kernel.nix`, DEVELOPMENT
   and DEBUG). First compile errors of the ported patches show here.
3. Every kext of the default collection at the tip, then `rebase/build-kc.nix` (members and order
   of the pin collection, `omitKexts` for the six new ones), `kc_overlap.py`, the launcher's
   offline load check (no bench). Decide Risk 1 here.
4. Tip libSystem with the spawn fix, tip minimal image, RAM root from it
   (`rebase/tools/stage_ramdisk_rebase.py` via `PCIE_STAGER`), QEMU image (`rebase/qemu-image.nix`:
   the QEMU stream's two recipes and three patches, with the tip source).
5. QEMU: the tip kernel collection, kexts and root with the M1's binaries, autopilot subset that
   passes there today, against a pin run of the same command on the same day.
6. M1, one area per boot series (`rb01`, …), each next to its pin result: shell on 1 CPU; 8 CPUs,
   3 boots (this does not re-prove the corecrypto hang, 1 in 9 on the unpatched kext: a 20-boot
   run goes to the overnight batch); Wi-Fi scan/join/DHCP/ping; GPU + display + compositor
   autopilot (20 phases, as `ds09`); sound (`m1-sound/tools`, `kern.pd_speaker.hold_probe=1`);
   Touch Bar (`m1-touchbar/tools`); boot timeline (`bs_timeline.py`, first frame ≤ 7.5 s); the
   whole desktop as `tools/gpu_desktop.sh`. Always `PCIE_KC=<tip collection>`, `PCIE_CHAIN` off,
   120 to 600 s, never `pdnvme=`.
7. Equivalence table, default changes as a patch for the owner, what can be dropped.

## Needed from the owner
- **QEMU runs of the tip** need their own template and input cache. Either the QEMU stream adds an
  environment variable for `qd_image.py`'s output root (`OUT`, now fixed to `runs/qemu-dev`), or
  the rebase stream runs that stream's unmodified `qd_image.py`/`qd_run.py` from a wrapper of its
  own that points them at `runs/qemu-dev-rebase` (no file of theirs edited, nothing shared
  written). The second needs no one else; say if the first is preferred.
- **Cocoa stream:** its patches `0004`, `0005`, `0006` do not apply at the tip; `0001` is upstream.

## Open, not in this stream: our audio backend on upstream's new coreaudiod
Owner decision (11 November): the equivalence proof runs the pin-built audio server and
frameworks on the tip's kernel and libSystem; the port is a follow-up.

What upstream's coreaudiod at the tip is (`src/Userspace/coreaudiod/coreaudiod.c`, 1721 lines):
a server for IOAudioFamily engines only. It finds every `IOAudioEngine` in the registry, opens
its user client (status buffer, `performClientIO` trap, register/unregister client buffer),
runs one IO thread per engine that mixes the clients' shared rings one period ahead of the
engine's playback head, and since the pin has gained input streams (capture, input IOProcs),
several devices with stable ids by UID, default-device choice and per-device preferences, and
volume/mute through `IOAudioLevelControl`/`IOAudioToggleControl`. **It has no pluggable device
backend**: an engine is an IOKit service or nothing. Our patch added a second kind of engine to
the old single-device server (a table of `/dev/pdaudio` and `/dev/pdspeaker` character devices
with `kern.pd_sound`/`kern.pd_speaker` sysctls for volume, jack state and the speaker hold).

Two ways to follow, to be decided by the sound stream:
1. Port the `/dev` engines onto the new `struct engine` (they become two more devices beside
   any IOAudioEngine; the new multi-device, default-device and preference code then applies to
   them). The patch is a rewrite, about the size of the old one (640 lines).
2. Make PDAppleSound an IOAudioFamily driver (`IOAudioDevice`/`IOAudioEngine` with level and
   mute controls). Upstream's server then drives it unpatched, capture comes for free when the
   microphone path exists, and the lab carries no coreaudiod patch at all. IOAudioFamily is in
   the tree and unchanged between pin and tip. This is the direction upstream's design points to.

## What could be dropped once the tip is the default (so far)
`m1-smp/kernel-patches/0002`; the register-name block of the bring-up patch's `board_config.h`;
the three dirty checkouts (`sources/puredarwin-m1-desktop`, `-m1-bringup`, `-m1-firstboot`) as
build inputs, and with them the six store paths written into the collection chain; the T8010
override in `m1-dcp/kernel.nix` (upstream has a T8103 kernel package). Not droppable: everything
listed as "not fixed upstream" above.
