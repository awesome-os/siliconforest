# Canonical Metal with explicit real-device opt-in

This is darling-metal (MPL-2.0), using the reviewed `m1-metal` recipes and pins,
not a Finch Metal implementation. `0001-real-device-opt-in.patch` changes that
MPL-2.0 source under the same license. All 185 original exports remain, including
both exports of the previous canonical provider. Our probe declarations are ours.

`MTLCreateSystemDefaultDevice` returns nil and both enumeration APIs return empty
arrays unless `SF_METAL_ENABLE_REAL_DEVICE` equals **exactly `1`**. The disabled
observer API sets the output observer to nil. These paths return before Indium/
Vulkan initialization. Invalid values, including `true`, do not enable devices.
Opt-in retains Darling's actual device implementation and its known kernel exit
wait bug; this change does not repair that bug or claim window presentation.

`build.nix` places the framework and Indium/Iridium/mslc/LLVM support libraries in
one framework transaction. Absolute support paths live under its Versions/A/lib;
no runtime dependency on a disposable experiment directory. Nothing of the core
system or Vulkan is replaced. Licenses remain beside the framework sources/libs.

M1: canonical installation qualified at 05:38:33Z on 15 November 2026. Eleven
cold file hashes were verified before activation; originals are `.pre-finch`.
Main SHA256: `29000069da813065d8b1b0610496e18918e50e8e67007059482a9bef65415a61`.
Default/invalid-opt-in probes exit normally with no devices. The unchanged
Homebrew ffmpeg bottle now completes four H.264/HEVC HW/SW trials **without any
DYLD overrides**. All 600 pictures per decode mode match the prior qualified
Linux/native controls. Single end-to-end HW/SW rates: H.264 270.27/162.07 fps;
HEVC 269.78/170.16 fps (normal desktop load, includes startup and hashing).

Root selection, direct VT 600 hardware pictures, WAV/AAC HAL, fresh Terminal/
Activity Monitor opens, desktop process health and compatibility 18/18 pass.
The same Linux-built surface probe also runs on native M5; native returns real
devices, deliberately different from our default policy. Its first invalid nil
observer-callback invocation failed; the corrected nonnil block passes. No new
real Darling-device test was run, avoiding the known unkillable-process issue.

Build `build.nix` and `tests.nix` with Linux/Nix. For a private default-path trial,
set DYLD_FRAMEWORK_PATH to the staged System/Library/Frameworks and
DYLD_LIBRARY_PATH to Metal.framework/Versions/A/lib. The installed trial removes
all DYLD variables. Receipts (ignored): `runs/sffinch/install-Metal-v1/`,
`metal-darling-export-retention-v1.json`, `metal-root-ffmpeg-qualified-v1.json`,
`metal-policy-v2/candidate.json` and `m5-next-media-v2-reference.json`.
