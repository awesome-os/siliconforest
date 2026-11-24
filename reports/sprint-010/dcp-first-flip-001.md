# Display controller: Omacom's driver page-flips at 60 Hz under Darwin (SF-H H3/H4)

Boot `ft03` (10 November 2026). Collection from `integrations/puredarwin/m1-dcp/build-kc.nix`
(bridge, PDGpuDT, PDDRMDri, PDDcp; no Wi-Fi, no PDAgx), boot-args
`pdgpudt=2 pdgpudt_stringprops=1 pddispdt=2 pddcp=2`. One command: `DCP_TEST=1 tools/dcp_boot.sh <new-name> <collection>`.
Raw lines: `runs/m1-desktop-010/pcie-enum-attempt-ft03/guest/console.bin` (`PDDcp`, `dcp-flip-test`, `DL43`).

What runs: `drivers/gpu/drm/apple` from Omacom `linux-aurora` `integration/aurora` at `6a08a08e`, 27 files compiled
as they are plus a patch of about 40 changed lines (a variable name and stage log lines), as `PDDcp.kext` on the
compatibility layer, with the RTKit port from the GPU work, the display DART backend (`m1-dcp-dart`) and the
synthesized display device tree (`m1-gpu-dt`, equal to the live Linux tree apart from justified omissions).

Measured:
- Bind: DCP booted, main display, every IOMFB callback answered, DRM device registered, `/dev/dri/card0`.
- KMS from userspace (libdrm): 1 CRTC, 1 connector (connected, 286x179 mm), mode 2560x1600 at 60 Hz
  (clock 266630 kHz), primary plane with 14 formats and a second plane with 11.
- Two 16,384,000-byte dumb buffers created through the DART backend, mapped into the test process, and
  registered as framebuffers.
- First atomic commit with a modeset: `dcp_poweron`, `set_digital_out_mode` 2560x1600, finished; frame committed.
- Flipping between the two buffers for 2 s with flip-complete events read from the DRM fd:
  **121 flips in 2.009 s, 60.24 per second, interval min 16.617 ms, max 16.716 ms.** Test exit status 0.

Not established:
- Nobody was watching the panel. The firmware accepted every swap and reported completion at the panel's
  refresh rate; what was visible is unobserved.
- The test leaves its last buffer on screen; the text console is not restored.
- Events were taken with a blocking `read()`. `poll`/`select` wakeups on the DRM fd are still not implemented.
- PDAgx and PDDcp do not fit one collection yet (about 0x64000 bytes over), so no GPU buffer has been scanned out.
- Brightness, display sleep, the cursor plane and the fbdev console client are untested.

Boots on the way (each failed one step later; all causes were in our layer or glue, none in the driver):
`dp01` endless device-tree walk, `dp02` child-device creation, `dp03` fixed-clock provider, `dp04` platform bus
registration order, `dp05`/`dp06` managed coherent allocation, `ft02` 7.2 `fb_create` signature.
