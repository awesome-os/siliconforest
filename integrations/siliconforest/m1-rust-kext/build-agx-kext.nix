# Builds SFAgx.kext: the Asahi AGX driver (common code + G13) on the Linux layer, with the
# DRM core at Linux 7.2 (m1-drm72), the Apple mailbox/RTKit port (m1-gpu-rtkit) and the
# extras (m1-lkpi-extras). GPL-2.0 as a whole. The kext is built in the flake's arm64
# DRM-kext tree slice with lkpi/patches-layer and m1-drm72's series applied
# (lkpi.finalTree, lkpi.finalDrmkmod), and agx/kext added as SFAgx. SFDRMDri.kext (the
# /dev/dri character devices, a library of every DRM kext) is built from the same tree.
#
# $out/nix-support/build-commands has the compile and link lines ninja ran; the build fails
# if the defines LinuxKPI was compiled with differ from the ones bindgen saw (lkpi/cflags.nix).
# $out/nix-support/undefined is `nm -u` of the kext, for the collection build to check.
let
  inherit (import ./pins.nix) pd pkgs crossToolchain;
  lkpi = import ./lkpi;
  k = ./agx/kext;
  copied = pkgs.runCommand "siliconforest-agx-source" {} ''
    mkdir -p "$out/rust" "$out/rtkit"
    cp ${k}/CMakeLists.txt ${k}/Info.plist ${k}/glue.c ${k}/sf_agx_linux.c ${k}/SFAgxHost.cpp "$out/"
    cp -r ${lkpi.rtkitSrc}/. "$out/rtkit/"
    cp ${lkpi.agxObject}/asahi.o "$out/rust/asahi.o"
  '';
  bindgenDefines = pkgs.writeText "bindgen-defines"
    (pkgs.lib.concatMapStrings (d: "-D${d}\n") lkpi.cflags.defines);
in lkpi.drmKextFinal.overrideAttrs (old: {
  pname = "siliconforest-agx";
  src = pkgs.runCommand "siliconforest-agx-tree" {} ''
    cp -R ${lkpi.finalTree} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/SFAgx"
    cp -R ${copied} "$out/src/Kernel/Extensions/SFAgx"
    printf '\nadd_subdirectory(SFAgx)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix -t commands SFAgx.kext > build-commands.txt

    # the layout check: -D set of a LinuxKPI source in this kext vs what bindgen was given
    grep -m1 'linux_work\.c\.o -c' build-commands.txt | tr ' ' '\n' | grep '^-D' \
      | grep -v -e KBUILD_MODNAME -e LINUXKPI_PARAM_PREFIX | sed 's/\\"/"/g' | sort -u > defines.kext
    sort -u ${bindgenDefines} > defines.bindgen
    if ! diff defines.kext defines.bindgen; then
      echo "LinuxKPI in the kext and the Rust bindings were built with different defines" >&2
      exit 1
    fi
    echo "defines match: $(wc -l < defines.kext) -D flags"

    ninja -C build-nix SFAgx.kext SFDRMDri.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/SFAgx/SFAgx.kext
    test -f "$bundle/Contents/MacOS/SFAgx"
    mkdir -p "$out/System/Library/Extensions" "$out/nix-support"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    cp -R build-nix/src/Kernel/Extensions/SFDRMDri/SFDRMDri.kext "$out/System/Library/Extensions/"
    cp build-commands.txt "$out/nix-support/build-commands"
    cp build-nix/SFAgx.map "$out/nix-support/SFAgx.map"
    ${crossToolchain}/bin/arm64-apple-darwin20.4-nm -u "$bundle/Contents/MacOS/SFAgx" | sed 's/^ *//' | sort -u > "$out/nix-support/undefined"
    # the kext link accepts undefined symbols (the kernel and other kexts provide them at
    # collection time), so a layer function that nothing defines (a missing extern "C", a
    # misspelt name) links and then faults at its first call. Names of this layer must be
    # defined here; the three families below come from SFDRMDri and the kernel (sf_usb_*
    # is referenced by the USB bus glue, which no driver of this machine registers with).
    bad=$(grep -E '^_(pd|linux_|lkpi_)' "$out/nix-support/undefined" | grep -Ev '^_pd_(dri|lfile|usb)_' || true)
    if [ -n "$bad" ]; then
      echo "SFAgx: layer symbols that nothing defines:" >&2; echo "$bad" >&2
      exit 1
    fi
    runHook postInstall
  '';
})
