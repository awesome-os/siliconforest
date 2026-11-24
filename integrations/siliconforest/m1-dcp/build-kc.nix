# Kernel collection for SFDcp: the bridge collection (../m1-apcie-host/build-kc.nix, no Wi-Fi)
# plus SFGpuDT (publishes the display nodes), SFDRMDri and SFDcp; `--arg agx true` adds SFAgx.
# The kernel is ./kernel.nix (the pinned bring-up kernel + ./kernel-patches), not the pinned
# binary: see kernel-swap.nix.
# Boot-args: the ones m1-gpu-dt documents for publishing the display nodes, and sfdcp=1|2.
# Fails when fewer than 4 pages are left before the kernel's __TEXT (see
# ../m1-rust-kext/build-agx-kc.nix for why).
#
# `--arg wifi true`: on ../m1-bwfm/build-kc.nix (bridge + IO80211Family + SFBwfm) instead.
# `--arg slim true`: SFDcp, SFDRMDri and SFAgx go in without their debug map (`strip -S`: the
# N_OSO/N_FUN stabs the linker writes for -g objects; 19381 of SFDcp's 19842 symbol-table
# entries). A kext's __LINKEDIT is part of what it occupies in the collection: 540672 ->
# 65536 bytes for SFDcp, 425984 -> 81920 for SFAgx. Code and data are not touched, and the
# link maps (nix-support/*.map, tools/symbolize.sh) stay. Wi-Fi + SFAgx + SFDcp only fits
# with this. The default since boots ws04 and ws05; `--arg slim false` keeps the debug map.
{ agx ? false, wifi ? false, slim ? true }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/build-kc.nix { } else
let
  base = if wifi then import ../m1-bwfm/build-kc.nix else import ../m1-apcie-host/build-kc.nix;
  tc = (import ../m1-rust-kext/pins.nix).crossToolchain;
  noDebugMap = kext: if !slim then kext else pkgs.runCommand "${kext.name}-no-debug-map" { } ''
    mkdir -p $out
    cp -R ${kext}/System ${kext}/nix-support $out/
    chmod -R u+w $out
    for b in $out/System/Library/Extensions/*.kext/Contents/MacOS/*; do
      ${tc}/bin/arm64-apple-darwin20.4-strip -S "$b"
    done
  '';
  dcp = noDebugMap (import ./build-kext.nix);
  agxKext = noDebugMap (import ../m1-rust-kext/build-agx-kext.nix);
  gpuDt = import ../m1-gpu-dt/build-kext.nix;
  needle = "  -o kernel";
  # the kernel with ../m1-dcp/kernel-patches (sf_lfile descriptors: SCM_RIGHTS, select/poll/
  # kevent wakeups) in place of the pinned one; the layer needs its sf_lfile_wakeup
  onKernel = import ./kernel-swap.nix;
  pkgs = (import ../m1-rust-kext/pins.nix).pkgs;
  extra = if agx then ''  -kext "${agxKext}/System/Library/Extensions/SFAgx.kext" \
      '' else "";
in base.overrideAttrs (old:
  assert builtins.stringLength old.buildPhase
    != builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase);
  {
    nativeBuildInputs = (old.nativeBuildInputs or [ ]) ++ [ pkgs.python3 ];
    buildPhase = ''
      set -o pipefail
      {
      '' + builtins.replaceStrings [ needle ] [ ''  -kext "${gpuDt}/System/Library/Extensions/SFGpuDT.kext" \
      -kext "${dcp}/System/Library/Extensions/SFDRMDri.kext" \
      ${extra}-kext "${dcp}/System/Library/Extensions/SFDcp.kext" \
      -o kernel'' ] (onKernel old.buildPhase) + ''
      } 2>&1 | tee kc-build.log
      # The launcher's image gate, reproduced (tools/kc_overlap.py): kc-builder puts the
      # collection's __PRELINK_INFO after the last kext and accepts it running into the
      # kernel's __TEXT; such a collection is refused at boot time.
      python3 ${../m1-rust-kext/tools/kc_overlap.py} kernel
    '';
  })
