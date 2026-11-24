# SPDX-License-Identifier: MIT
# The kernel collection the M1 runs ("kernel 48", qualified 16 November 2026) from source, under
# the SiliconForest names, in one recipe: 57 members, each compiled here from the renamed tree.
#
# Kernel 48 itself was the end of a chain of recipes (m1-firefox-kernel, m1-devfs-clone,
# m1-upl-private, m1-killable-waits, m1-ext4-eof, upstream-12377, m1-wifi-pm,
# m1-ext4-journal-cache) that each replaced one module of the collection before and kept the
# others as built binaries. Those recipes compare against collections built under the old names
# and are left as they are. This recipe names the same members, takes each from the leaf recipe
# and the patches the chain used, and links them once. No built binary, no source snapshot from
# the store and no file outside the tree is an input.
#
# Five members of kernel 48 were built from an earlier revision of their lab directory than the
# tree has now (the chain froze their source trees). For each, ./back-to-48/<name>.remove (files
# the tree has gained since) and ./back-to-48/<name>.patch are the exact difference "the tree's
# source today -> the source kernel 48 was compiled from", applied to the member's source tree
# before anything else:
#   dcp    SFDcp, SFDRMDri   (later: both DARTs, hotplug generations, the DCP tunnel bridge)
#   agx    SFAgx             (later: aligned I/O-memory reads in the RTKit port)
#   avd    SFAppleAVD        (later: one change in sf_avd_core.c)
#   bwfm   IO80211Family, SFBwfm (later: the tree's own power-save API; kernel 48 has the port
#                            of it, ../m1-wifi-pm/fxnet-pm.patch and pm-admission.patch)
#   cio    SFAppleCIO        (later: the dock work after "cio29")
# Deleting a patch moves that member to the tree's present source; that is a new kernel, not 48.
#
#   nix-build build-kc.nix --no-out-link                 the collection ($out/kernel)
#   nix-build build-kc.nix -A members --no-out-link      every member bundle, unstripped
# ./compare-members.py proves each member against kernel 48's binary.
let
  source = import ../rebase/pd.nix;
  inherit (source) pd pkgs;
  p = pd.packages.x86_64-linux;
  strip = "${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-strip";
  ext = "System/Library/Extensions";
  # The tree names are the chain's (renamed): a source file's path is in its assertion strings.
  patched = name: tree: steps: pkgs.runCommand name { nativeBuildInputs = [ pkgs.patch ]; } ''
    cp -R ${tree} "$out"
    chmod -R u+w "$out"
    ${pkgs.lib.concatMapStrings (s:
      if s ? remove then "while read -r f; do rm \"$out/$f\"; done < ${s.remove}\n"
      else "patch -d \"$out${s.dir or ""}\" --batch --forward --fuzz=0 -p1 < ${s.patch}\n") steps}
  '';

  # the kernel: the lab series (../rebase/kernel.nix), the devfs clone lifetime fix and the
  # private-UPL exclusion
  kernel = (import ../rebase/kernel.nix { }).overrideAttrs (old: {
    pname = "siliconforest-kernel-t8103-devfs-upl-private";
    name = "siliconforest-kernel-t8103-devfs-upl-private-0.1";
    patches = (old.patches or [ ]) ++ [ ../m1-devfs-clone/kernel.patch ../m1-upl-private/kernel.patch ];
  });

  # upstream's boot kexts with the native platform expert, the console-return framebuffer and
  # the HID family of rev002 (as ../rebase/build-kc.nix makes them), then the PCI profile
  bootKexts = (import ../rebase/build-kc.nix { platform = import ../m1-native-boot/build-platform.nix; }).kexts;
  enumerationBase = import ../m1-thunderbolt/build-enumeration-base.nix { kexts = bootKexts; };

  # kernels 46 to 48: upstream's IOPCI scan and USB changes (../upstream-12377)
  selected = import ../upstream-12377/build-kexts.nix;
  xhci = (import ../m1-usb-dwc3/build-host.nix).overrideAttrs (old: {
    pname = "siliconforest-qualified-xhci-upstream-usb";
    patches = (old.patches or [ ]) ++ [ ../upstream-12377/patches/aaf3604d-usb.patch ];
  });
  profile = pkgs.runCommand "siliconforest-upstream-qualified-enumeration-profile" { nativeBuildInputs = [ pkgs.python3 ]; } ''
    dest="$out/${ext}"
    mkdir -p "$dest"
    cp -R ${selected}/${ext}/IOPCIFamily.kext "$dest/"
    chmod -R u+w "$dest"
    # Preserve every deployed bridge/Wi-Fi admission decision in IOPCIFamily metadata.
    cp ${enumerationBase}/${ext}/IOPCIFamily.kext/Contents/Info.plist "$dest/IOPCIFamily.kext/Contents/Info.plist"
    cp -R ${selected}/${ext}/IOUSBCompositeDriver.kext "$dest/"
    # two network drivers of the same build: not linked (kernel 48 has neither), present so that
    # the profile step below is the chain's own, which expects PCI endpoint matches to remove
    cp -R ${selected}/${ext}/SFFBSDem.kext "$dest/"
    cp -R ${selected}/${ext}/SFFBSDigc.kext "$dest/"
    cp -R ${xhci}/${ext}/RavynXHCIPort.kext "$dest/"
    chmod -R u+w "$dest"
    python3 ${../m1-thunderbolt/tools/enumeration-usb-personalities.py} "$dest/RavynXHCIPort.kext/Contents/Info.plist"
    python3 ${../m1-thunderbolt/tools/enumeration-pci-profile.py} "$dest"
  '';
  hid = import ../m1-firefox-kernel/build-hid-readiness.nix;

  # the built-in keyboard and trackpad with the Touch Bar notes and the K14 gesture ring
  spihidBase = import ../m1-touchbar/build-spihid.nix;
  mt = ../m1-trackpad-child/multitouch;
  spihid = spihidBase.overrideAttrs (old: {
    pname = "siliconforest-firefox-k14-touchbar";
    src = pkgs.runCommand "siliconforest-m1-spi-hid-touchbar-source" { } ''
      cp -R ${old.src} "$out"
      chmod -R u+w "$out"
      dest="$out/src/Kernel/Extensions/SFM1SPIHID"
      cp ${mt}/m1tp_mt.c ${mt}/m1tp_mt.h ${mt}/SFM1SPIHIDMultitouch.inc ${mt}/sf_tp_gesture_ring.h "$dest/"
    '';
  });

  # Wi-Fi: the family and the driver as kernel 48 has them, the driver with the power-save port
  wifiBase = import ../m1-bwfm/build-kexts.nix { };
  wifiTree = patched "siliconforest-bwfm-tree" wifiBase.src [ { patch = ./back-to-48/bwfm.patch; } ];
  wifi = wifiBase.overrideAttrs (_: { src = wifiTree; });
  wifiPm = wifiBase.overrideAttrs (old: {
    pname = "siliconforest-qualified-bwfm-pm";
    src = wifiTree;
    patches = (old.patches or [ ]) ++ [ ../m1-wifi-pm/fxnet-pm.patch ../m1-wifi-pm/pm-admission.patch ];
  });

  # display: SFDcp with killable waits; SFDRMDri with the devfs clone API and its counters
  dcpBase = import ../m1-dcp/build-kext.nix;
  dcpTree = patched "siliconforest-dcp-kernel48-tree" dcpBase.src [ { remove = ./back-to-48/dcp.remove; } { patch = ./back-to-48/dcp.patch; } ];
  dcp = dcpBase.overrideAttrs (_: {
    pname = "siliconforest-dcp-killable";
    src = patched "siliconforest-dcp-killable-tree" dcpTree [ { patch = ../m1-killable-waits/killable.patch; } ];
  });
  dri = dcpBase.overrideAttrs (old: {
    pname = "siliconforest-dri-devfs-clone";
    src = patched "siliconforest-dri-devfs-clone-tree" dcpTree [
      { patch = ../m1-devfs-clone/api.patch; }
      { patch = ../m1-devfs-clone/dri.patch; }
      { patch = ../m1-devfs-clone/dri-stats.patch; }
    ];
    nativeBuildInputs = (old.nativeBuildInputs or [ ]) ++ [ pkgs.python3 ];
    postConfigure = (old.postConfigure or "") + "\n" + ''
      python3 ${../m1-devfs-clone/overlay-header.py} build-nix/src/Kernel/xnu/xnu_header_install src/Kernel/xnu/bsd/miscfs/devfs/devfs.h
    '';
    buildPhase = ''
      runHook preBuild
      ninja -C build-nix SFDRMDri.kext
      runHook postBuild
    '';
    installPhase = ''
      runHook preInstall
      mkdir -p "$out/${ext}" "$out/nix-support"
      cp -R build-nix/src/Kernel/Extensions/SFDRMDri/SFDRMDri.kext "$out/${ext}/"
      cp devfs-header-overlay.json "$out/nix-support/"
      runHook postInstall
    '';
  });

  # GPU with killable waits
  agxBase = import ../m1-rust-kext/build-agx-kext.nix;
  agx = agxBase.overrideAttrs (_: {
    pname = "siliconforest-agx-killable";
    src = patched "siliconforest-agx-killable-tree" agxBase.src [
      { remove = ./back-to-48/agx.remove; }
      { patch = ./back-to-48/agx.patch; }
      { patch = ../m1-killable-waits/killable.patch; }
    ];
  });

  # video decoder: the import cache, then the context statistics
  avdBase = import ../m1-avd/build-kext.nix;
  avd = avdBase.overrideAttrs (old: {
    pname = "siliconforest-avd-context-stats";
    src = patched "siliconforest-firefox-avd-import-tree" avdBase.src [
      { patch = ./back-to-48/avd.patch; }
      { patch = ../m1-firefox-kernel/avd-import-cache.patch; dir = "/src/Kernel/Extensions/SFAppleAVD"; }
    ];
    patches = (old.patches or [ ]) ++ [ ../m1-avd-context-stats/context-stats.patch ];
  });

  # ext4: clean downgrade, the end-of-file fix, the journal read cache
  ext4 = (import ../m1-storage/build-ext4.nix).overrideAttrs (old: {
    pname = "siliconforest-ext4-journal-cache";
    name = "siliconforest-ext4-journal-cache-0.1";
    patches = (old.patches or [ ]) ++ [ ../m1-ext4-eof/eof.patch ../m1-ext4-journal-cache/journal-cache.patch ];
  });

  # Thunderbolt (hardware trials paused: built, as kernel 48 has it, nothing more)
  cioBase = import ../m1-thunderbolt/build-kext-base.nix {
    connectionManager = true; pciEnumeration = true; endpointMapper = true; pciMSI = true;
  };
  cio = cioBase.overrideAttrs (_: {
    src = patched "siliconforest-apple-cio-tree" cioBase.src [ { remove = ./back-to-48/cio.remove; } { patch = ./back-to-48/cio.patch; } ];
  });

  # every member that is not one of upstream's boot kexts, in kernel 48's link order
  added = [
    { name = "SFM1SPIHID"; from = spihid; }
    { name = "SFApcieT8103Bridge"; from = import ../m1-apcie-host/build-kext.nix; }
    { name = "IO80211Family"; from = wifi; }
    { name = "SFBwfm"; from = wifiPm; }
    { name = "SFGpuDT"; from = import ../m1-gpu-dt/build-kext.nix; }
    { name = "SFDRMDri"; from = dri; }
    { name = "SFAgx"; from = agx; }
    { name = "SFDcp"; from = dcp; }
    { name = "SFCpuPower"; from = import ../m1-native-boot/reboot/build-cpupower.nix; }
    { name = "SFAppleSound"; from = import ../m1-sound/build-kext.nix; }
    { name = "SFAppleTouchBar"; from = import ../m1-touchbar/build-kext.nix; }
    { name = "SFAppleAVD"; from = avd; }
    { name = "SFAppleNVMeRW"; from = import ../m1-storage/build-kext-rw.nix; }
    { name = "ext4"; from = ext4; }
    { name = "SFAppleTypeC"; from = import ../m1-typec/build-kext.nix; }
    { name = "RavynXHCIPort"; from = profile; }
    { name = "SFAppleCIO"; from = cio; }
  ];
  replaced = [
    { name = "IOPCIFamily"; from = profile; }
    { name = "IOUSBCompositeDriver"; from = profile; }
    { name = "IOHIDFamily"; from = hid; }
  ];
  # the boot kexts of the collection: the enumeration base with three members replaced
  kexts = pkgs.runCommand "siliconforest-kernel48-boot-kexts" { } ''
    dest=$out/${ext}
    mkdir -p $dest
    cp -RL ${enumerationBase}/${ext}/. $dest/
    chmod -R u+w $out
    ${pkgs.lib.concatMapStrings (m: ''
      test -f ${m.from}/${ext}/${m.name}.kext/Contents/MacOS/${m.name}
      rm -rf $dest/${m.name}.kext
      cp -RL ${m.from}/${ext}/${m.name}.kext $dest/${m.name}.kext
    '') replaced}
  '';
  # all 56 bundles as built (debug maps and local symbols still in), for the member proof
  members = pkgs.runCommand "siliconforest-kernel48-members" { } ''
    mkdir -p $out/kexts $out/kernel
    cp -RL ${kernel}/System/Library/Kernels/. $out/kernel/
    cp -RL ${kexts}/${ext}/. $out/kexts/
    chmod -R u+w $out
    ${pkgs.lib.concatMapStrings (m: ''
      test ! -e $out/kexts/${m.name}.kext || rm -rf $out/kexts/${m.name}.kext
      cp -RL ${m.from}/${ext}/${m.name}.kext $out/kexts/${m.name}.kext
    '') added}
  '';
  collection = pkgs.callPackage (pd.outPath + "/nix/pkgs/toolchain/kc-arm64.nix") {
    kcTools = import ../m1-bringup/kc-tools-fixed.nix;
    inherit kernel kexts;
    kernelSource = kernel.src;
    kcBase = "fffffe0006000000";
    extraKexts = map (m: "${m.from}/${ext}/${m.name}.kext") added;
    omitKexts = [
      # as every collection of this machine
      "SFSerialHID" "SFTegraSDMMC" "SFGadgetFamily" "SFTegraUSB" "SFTegraXUSB" "SFTegraDisplay" "SFFBSDem"
      # as the disk-root collections (../m1-storage/build-kc-rw.nix)
      "SFSg2002Eth" "SFSg2002SD" "SFSun60iDisplay" "SFQcomGeniI2C" "SFQcomTLMM" "SFPowerVR"
      # replaced by a member of `added`
      "ext4" "RavynXHCIPort"
    ];
  };
in
assert source.tip;
collection.overrideAttrs (old: {
  name = "siliconforest-kc-kernel48";
  nativeBuildInputs = (old.nativeBuildInputs or [ ]) ++ [ pkgs.python3 ];
  SF_SELECTED_NETWORK_MODULES = "0";
  # As kernel 48's last step: every bundle without debug map and local symbols (the proof that
  # no other section byte changes is kc-strip-proof.json), the link, the collection's property
  # list without its structural white space, the overlap check.
  buildPhase = ''
    python3 ${../upstream-12377/strip-candidate-kc-inputs.py} ${pkgs.writeText "kernel48-link-phase.sh" old.buildPhase} phase-stripped.sh ${strip} "$PWD/stripped"
    set -o pipefail
    { . ./phase-stripped.sh; } 2>&1 | tee kc-build.log
    python3 ${../m1-thunderbolt/tools/compact_kc_prelink.py} kernel
    python3 ${../m1-rust-kext/tools/kc_overlap.py} kernel
  '';
  installPhase = ''
    mkdir -p $out/nix-support
    cp kernel $out/kernel
    cp kc-build.log kc-strip-proof.json kc-prelink-compact-proof.json $out/nix-support/
  '';
  passthru = (old.passthru or { }) // { inherit kernel kexts members added replaced ext4 selected; };
})
