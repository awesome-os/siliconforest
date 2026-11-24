<!-- Research input for the roadmap tickets; produced by a read-only research agent on 2026-11-09. Unverified on hardware. -->

# (A) Sound on J293: technical basis

**1. Hardware inventory** (read from `/home/chrisk/source/aurora-linux/arch/arm64/boot/dts/apple/t8103-j293.dts` and `t8103.dtsi`)
- **Speakers:** 4x TI TAS5770L (`"ti,tas5770l","ti,tas2770"`), two woofers per channel, stereo.
  - i2c1: 0x31 Left Rear, 0x32 Left Front. i2c3: 0x34 Right Rear, 0x35 Right Front.
  - Shared shutdown GPIO `pinctrl_ap 181`, shared IRQ `pinctrl_ap 182` (level low).
  - IMON/VMON sense data returns in TDM slots (`ti,imon-slot-no`, `ti,vmon-slot-no`). The link uses MCA clusters 0 and 1.
- **Headphone jack:** Cirrus CS42L83 on i2c2 at 0x48, reset GPIO `pinctrl_nub 11`, IRQ `pinctrl_ap 183`, `cirrus,ts-inv`, MCA cluster 2. It also carries the headset mic ("Headset Capture").
- **I2C:** `apple,t8103-i2c` (PASemi core). i2c1 at 0x235014000 (AIC 628), i2c2 at 0x235018000 (AIC 629), i2c3 at 0x23501c000 (AIC 630), power domains `ps_i2cN`.
- **I2S (MCA):** 0x238400000 plus switch at 0x238300000, AIC 619-624, power domains `ps_audio_p` and `ps_mca0..5`, reset via `ps_audio_p`, clocks NCO 0-4 (0x23b044000, 900 MHz reference).
- **DMA:** ADMAC at 0x238200000, 24 channels (tx/rx a/b per cluster), AIC 626, `ps_sio_adma`, behind `sio_dart` (0x235004000, stream 2, AIC 635, `ps_sio_cpu`).
- **Internal mics:** PDM via the AOP coprocessor (RTKit/EPIC at 0x24ac00000, `aop_admac` 0x24a980000 on AIC 321, `aop_dart` stream 7). Not on the MCA.
- **Firmware/calibration:** none for T8103. m1n1 `dt_set_speaker_safety()` (`src/kboot.c:694`) returns early unless the chip is T8140.

**2. Linux stack and m1n1**

| File | Lines | Licence |
|---|---|---|
| `sound/soc/apple/mca.c` | 1323 | GPL-2.0-only |
| `sound/soc/apple/macaudio.c` | 2010 | GPL-2.0-only |
| `drivers/dma/apple-admac.c` | 1017 | GPL-2.0-only |
| `sound/soc/codecs/tas2770.c` / `.h` | 1012 / 164 | GPL-2.0 |
| `sound/soc/codecs/cs42l42.c` / `cs42l83-i2c.c` | 2889 / 240 | GPL-2.0-only |
| `drivers/i2c/busses/i2c-pasemi-core.c` / `-platform.c` | 465 / 113 | GPL-2.0-only |
| `drivers/clk/clk-apple-nco.c` | 340 | GPL-2.0-only OR MIT |
| `drivers/pinctrl/pinctrl-apple-gpio.c` | 619 | GPL-2.0-only |
| `sound/soc/apple/aop_audio.rs` (Rust, needs `drivers/soc/apple/aop.rs`) | 1017 | GPL-2.0-only OR MIT |

- Framework dependencies (from the `#include` lists): ASoC (DPCM, DAPM, kcontrols, jack, `simple_card_utils`), regmap, dmaengine and `dmaengine_pcm`, clk, reset, pm_domain, gpiod, regulator, hwmon.
- m1n1 (MIT) has register-level references under `sources/m1n1-baseline/proxyclient/`:
  - `m1n1/hw/admac.py` (421), `mca.py` (110), `nco.py` (89), `i2c.py` (251).
  - `experiments/speaker_amp.py` (148): a complete NCO, MCA, ADMAC and DART playback sequence, written for the Mac mini. It uses ADT paths `/arm-io/i2c1`, `admac-sio`, `dart-sio`, `mca-switch`, `nco` and the `PS_AUDIO_P` reset.
  - `experiments/audio_capture.py` (132), `aop_audio.py` (216).
  - The only codec models are `cs42l84.py` and `ssm3515.py`; neither chip is on the J293.

**3. PureDarwin today** (`sources/puredarwin-m1-desktop/src`)
- `Kernel/Extensions/IOAudioFamily`: Apple's family vendored unmodified (APSL, 13 .cpp sources), added unconditionally in `Extensions/CMakeLists.txt:110`.
- `RavynHDAudio` (1148 lines, playback only) is the model driver: `IOAudioDevice` plus `IOAudioEngine`. It overrides the arm64-only pure virtual `driverDesiresHiResSampleIntervals()`, so arm64 was considered.
- `Userspace/coreaudiod/coreaudiod.c` (1079) mixes clients into the first output stream only. There is no capture path: `AudioQueueNewInput` returns `kAudioQueueErr_InvalidDevice` (`Frameworks/AudioToolbox/AudioQueue.c:519`).
- User API: `Frameworks/CoreAudio` (1702 lines), `AudioToolbox` (2619: AudioQueue, AudioUnit, AudioConverter), test tool `Userspace/pcmplay` (151).
- `PDI2CFamily` (188 lines): abstract `PDI2CController::transferLocked()`, `publishDevices()`, `PDI2CDevice` nubs. `PDDesignWareI2C` (243) is a host-controller example.
- The lab's M1 kernel-collection member list (`host_progress.py:628`) includes PDI2CFamily but not IOAudioFamily. Inferred: audio has never run on the M1.
- `fbsdkpi` has no sound or dmaengine code; `regmap.h` is a dummy header.

**4. Options and recommendation**
- **(i) LinuxKPI port:** rejected. It needs ASoC, DAPM, regmap and dmaengine, none of which exist in LinuxKPI, and the result would still need an ALSA-to-IOAudioEngine shim.
- **(ii) BSD port (from memory, not read; the lab's OpenBSD checkout holds no audio files):** OpenBSD has ISC drivers `aplmca`, `apldma`, `aplaudio`, `apliic` and `tascodec` (TAS2770), a few hundred lines each. I am unsure it has a CS42L83 driver. Its `audio(9)` trigger/block-interrupt model maps closely onto `IOAudioEngine`. FreeBSD has no Apple Silicon audio.
- **(iii) Native IOKit (recommended):** write IOKit drivers, using OpenBSD's ISC files as the structural base if a checkout confirms they exist, and Linux and m1n1 as register references.
  - `PDAppleI2C` as a `PDI2CController` subclass.
  - `PDAppleMCAEngine` as an `IOAudioEngine`: ADMAC ring in DART-mapped memory, `takeTimeStamp()` on the ADMAC period IRQ.
  - `PDCS42L83` and `PDTAS2770` as `PDI2CDevice` clients exposing `IOAudioLevelControl`/`IOAudioToggleControl`.
  - Anything derived from the GPL files stays GPL-2.0 with the SPDX line and notices.

**5. Safety**
- Linux only enables speakers for whitelisted models (`macaudio_j293_cfg = {true, AMP_TAS5770, SPKR_2W, stereo, gain 15, safe_vol -20}`, `macaudio.c:1779`); otherwise it logs "driver can't assure safety on this model".
- It caps "Amp Gain Volume" at 15 (`macaudio_set_speaker`).
- It holds the volume limit at step 1 (`macaudio_vlimit_unlock`) until a userspace process owns "Speaker Volume Unlock" and every volume control, and keeps pinging within a timeout (`macaudio_vlimit_update`).
- That process models the speakers from the "Speaker Sense Capture" PCM. `tas2770.c:173` notes that broken IVSENSE "would break software speaker protection". The override is `please_blow_up_my_speakers`.
- The daemon (speakersafetyd) is not in the kernel tree; that it uses a per-model thermal model is from memory.
- **Safe scope:** headphones first. Leave GPIO 181 low so the amps stay in shutdown. The speaker ticket is separate and must ship a hard kernel cap (fixed amp gain 15 or lower, digital volume clamp, no user override) before any sound.
- **Damage paths:** a wrong TDM slot or format producing full-scale noise; DC; an ADMAC underrun looping a buffer.

**6. Platform dependencies**

| Piece | In the lab today |
|---|---|
| AIC | Yes (`PDAppleAIC`) |
| PMGR power domains | Yes, as `pmgr_adt_power_enable` logic inside `PDApcieT8103Link.cpp` (1277 lines); needs factoring out |
| DART | Code exists (`m1-apcie-host/dart/pd_dart_map.cpp`, 649 lines) but is hard-wired to `dart-apcie0` stream 1; needs a `dart-sio` stream 2 instance |
| GPIO | No family; pins are poked by MMIO (`m1-keyboard/m1kbd_pio.c`, CLKREQ in `PDApcieT8103Link.cpp`); needs a small GPIO and pin-interrupt service |
| Apple I2C, NCO, ADMAC, MCA | Missing |
| SMC/RTKit | Client exists (`PDAppleSMC.cpp`, 898 lines); only needed for AOP mics |

**7. Work items**

| # | Item | Acceptance test on hardware | Rough size |
|---|---|---|---|
| A1 | `PDAppleI2C` | Read CS42L83 device ID at i2c2/0x48 and TAS2770 revision at 0x31/32/34/35 | ~400 lines |
| A2 | GPIO and PMGR helpers | Toggle `pinctrl_nub 11` reset; CS42L83 answers only when out of reset | ~400 |
| A3 | NCO, MCA, ADMAC and `dart-sio` engine | `pcmplay` runs with a monotonic sample clock and no DART faults, with the codec muted | ~1500 |
| A4 | CS42L83 playback, jack detect, volume | 1 kHz tone in headphones at 44.1/48 kHz; plug and unplug events logged | ~800 |
| A5 | coreaudiod on M1, default device, volume persistence | Tone plays through the CoreAudio API, not just the test tool | small |
| A6 | Speakers behind a hard cap, sense capture verified | Quiet tone per speaker; IMON/VMON slots carry data | ~700 + safety design |
| A7 | Capture: headset mic, then AOP mics (needs coreaudiod input and an AOP/EPIC client) | Record and play back a clip | largest unknown |

**Risks:** ADT node and property names for the codecs on J293 are unverified; the PureDarwin kexts match the ADT, not the Linux FDT. IOAudioFamily is unproven on this arm64 kernel collection. Clocking and TDM details (`MACAUDIO_DAI_FMT`, inverted clocks, bclk ratio 64) are easy to get wrong.

---

# (B) Bluetooth (BCM4378) on J293: technical basis

**1. Hardware inventory**
- PCI function `14e4:5f69` at 1:0.1, already enumerated with BAR0 = 0xc0c10004 and BAR2 = 0xc0800004 (`reports/sprint-010/pcie-darwin-enumeration-001.md:24`).
- Shares with Wi-Fi: radio power (SMC `gP0d`), PERST#, and `dart-apcie0` stream 1.
- **Interrupt:** MSI is mandatory. The probe comment says legacy interrupts cause an IRQ storm.
- **DMA:** 32-bit window, `BCM4377_DMA_MASK 0xfffffe00`.
- **Reset:** probe does a function-level reset, then waits 100 ms before touching any BAR ("device will crash" otherwise), then writes the config-space window registers in `bcm4377_init_cfg()`.
- **Firmware:** `brcm/brcmbt4378b1-apple,honshu-m.bin` and `.ptb`. The name is built from chip, OTP stepping, board type and OTP vendor (`bcm4377_request_blob`, with a fallback without vendor).
- **Firmware boot (`bcm4377_boot`):** the image is copied into DMA memory, its address and size written to BAR2 `FW_LO/HI/SIZE`, doorbell at BAR0 0x140, then wait up to 5 s for bootstage 2.
- **Calibration and identity:**
  - m1n1 copies ADT `/arm-io/bluetooth` properties `bluetooth-taurus-calibration-bf` and `bluetooth-taurus-calibration` to `brcm,taurus-bf-cal-blob` and `brcm,taurus-cal-blob` (`kboot.c:1253-1300`).
  - Stepping b1 uses the beamforming blob (`bcm4378_send_calibration`).
  - The BD address comes from ADT `/chosen` `mac-address-bluetooth0`, byte-swapped into `local-bd-address` (`kboot.c:1185-1218`), and is set with vendor command 0xfc01.
  - Board type is `apple,honshu` (`t8103-j293.dts`).
  - Darwin sees the ADT directly, so the kext reads these properties itself (inferred from how the PCIe kext reads `/arm-io/pmgr`).

**2. Linux stack and m1n1**
- `drivers/bluetooth/hci_bcm4377.c`: 2764 lines, **GPL-2.0-only OR MIT**.
- Structure: 5 completion rings and 7 transfer rings (control, HCI, SCO, ACL in both directions), RTI setup (`bcm4377_setup_rti`), OTP parsing, chunked calibration and PTB upload (0xe6 and 0xcf byte chunks) in `bcm4377_hci_setup`, suspend/resume.
- BCM4378 quirks: `broken_mws_transport_config`, `broken_le_coded`, `broken_le_ext_adv_report_phy`.
- Dependencies: PCI, MSI, `dma_alloc_coherent`, firmware loader, skb, and the HCI core (26 call sites, including `hci_register_dev`, `__hci_cmd_sync`, `hci_recv_frame`).
- The host stack is `net/bluetooth`: 72,055 lines, GPL-2.0 (`hci_sync.c` 7857, `l2cap_core.c` 7995, `mgmt.c` 10771, `smp.c` 3833), plus BlueZ in userspace (from memory).
- m1n1 contributes only the calibration blobs and BD address above.

**3. PureDarwin today**
- No Bluetooth stack at all. `src/Frameworks/IOBluetooth/IOBluetooth.m` is a 23-line stub ("a system with no Bluetooth controller driver").
- No `AF_BLUETOOTH` in `xnu/bsd/sys/socket.h`, no netgraph or Bluetooth code in `fbsdkpi`, and `IOSerialFamily` is headers only.
- Useful hooks: `IOHIDFamily` has `IOHIDUserDevice`/`IOHIDResource`, so a userspace stack can publish keyboards and mice. Audio profiles would plug into `coreaudiod`/IOAudioFamily.

**4. Options and recommendation**
- **(i) LinuxKPI:** rejected. The driver is unusable without the Linux HCI core, which means importing ~72k lines of GPL socket-based stack into XNU.
- **(ii) BSD (all from memory):** OpenBSD removed its Bluetooth stack around 2014. FreeBSD has a netgraph stack (`ng_hci`, `ng_l2cap`, `ng_btsocket`) with USB/UART transports only. NetBSD has a BSD-licensed in-kernel stack (HCI, L2CAP, RFCOMM, `bthidev`). None has a BCM4377-family PCIe transport.
- **(iii) Native IOKit transport (recommended):** port `hci_bcm4377.c` to a `PDBcm4377` kext under its MIT option, following the PDBwfm pattern, and expose raw HCI (command, event, ACL, SCO packets) through an `IOUserClient`.
  - Keep the host stack in userspace as a `bluetoothd`. This avoids new kernel socket families and keeps the stack choice open.
  - Stack candidates (from memory; licences must be verified): Apache NimBLE (LE only), Zephyr host, Android's stack (dual-mode, heavy), BTstack (dual-mode with A2DP/HID, but restrictive licence), or the NetBSD stack in-kernel.
  - Staging: (1) HCI up and LE scan with a raw-HCI CLI and no stack. (2) Pairing and LE HID (HOGP) via `IOHIDUserDevice`, then classic HID. (3) A2DP/HFP as a virtual coreaudiod device.

**5. Safety**
- No hardware-damage path found.
- Regulatory: calibration and PTB (power tables) must be loaded before any radio use, as `bcm4377_hci_setup` does.
- Shared resources: the BT driver must never toggle `gP0d`, PERST# or the DART stream while Wi-Fi is up. Conversely, PDBwfm's power-down path (`PDBwfm.cpp`, `kDownPower`) takes the port and radio down under Bluetooth. The bridge needs refcounted ownership.
- The BD address, calibration blobs, firmware and link keys are device identifiers or secrets and must not be committed.

**6. Platform dependencies**

| Piece | Status |
|---|---|
| Bridge and link-up | Exists (`m1-apcie-host`) |
| MSI | Exists: `PDApcieSetupMSI` allocates per device (Wi-Fi has AIC 704) |
| DART | Exists: `pd_dart_pool_alloc()`, 16 MiB bounce pool at IOVA 0x4000000, within the 32-bit mask; shared with Wi-Fi, so capacity needs checking |
| Radio power | SMC `gP0d` client exists (`PDAppleSMC.cpp`) |
| Firmware loading | Pattern exists: `loadfirmware()` at `PDBwfm.cpp:829` via `vnode_open` |
| BAR mapping | Use `kIOMapPostedWrite` (known rule) |
| Port/power refcount, HCI user-client ABI, host stack | Missing |

**7. Work items**

| # | Item | Acceptance test on hardware | Rough size |
|---|---|---|---|
| B1 | Kext skeleton: FLR, window config, OTP parse | Log shows stepping `b1`, vendor `m` and the chosen firmware name | ~500 lines |
| B2 | Rings, MSI, firmware boot, RTI | "bootstage 2" logged; control ring opens | ~1500 |
| B3 | HCI open, calibration, PTB, BD address | `HCI_Reset`, `Read_BD_ADDR` and `Read_Local_Version` return success, with Wi-Fi still pinging | ~600 |
| B4 | Raw-HCI user client and `pd-hcitool` | LE scan lists nearby advertisers | ~600 |
| B5 | Bridge power refcount and lifecycle | Wi-Fi off/on and sleep/wake leave Bluetooth working | small |
| B6 | Host-stack selection spike, then `bluetoothd` with LE pairing and HOGP | A Bluetooth keyboard types into Terminal | large |
| B7 | Classic HID, then A2DP sink device in coreaudiod | Headphones play `pcmplay` output | large |

**Risks:**
- Host-stack choice and licence dominate the schedule.
- Wi-Fi/Bluetooth coexistence behaviour is unknown.
- SCO rings are untested even conceptually here.
- The firmware image has to fit in the shared DART pool; its size was not checked.
- Whether BCM4378 sets `disable_aspm` was not read.
- ADT property names are verified only through m1n1's code, not on this machine.