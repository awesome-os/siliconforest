# (b) SFRtkitProbe.kext: the port (src/) and the probe (probe/) in a kext built the way
# ../m1-rust-kext/build-lkpi-kext.nix builds SFRustLKPI: the flake's arm64 DRM-kext tree
# slice with lkpi/patches-layer applied (lkpi.tree), this kext added to it, the layer
# compiled into the kext by cmake (add_fbsdkpi_driver ... LINUXKPI_DRM).
#   $out/System/Library/Extensions/SFRtkitProbe.kext
#   $out/nix-support/build-commands   the compile and link lines ninja ran
#   $out/nix-support/objects/         the objects of this directory's C and C++ files
#   $out/nix-support/undefined        symbols the kext imports (resolved, or not, by the KC)
# The build fails if rtkit.c was compiled with other defines than the ones the Rust
# bindings were generated with (lkpi/cflags.nix): a differing define can mean a differing
# struct layout between the Rust wrapper and this C.
let
  inherit (import ../m1-rust-kext/pins.nix) pd pkgs crossToolchain;
  lkpi = import ../m1-rust-kext/lkpi;
  copied = pkgs.runCommand "siliconforest-rtkit-probe-source" {} ''
    mkdir -p "$out"
    cp ${./probe/CMakeLists.txt} "$out/CMakeLists.txt"
    cp ${./probe/Info.plist} "$out/Info.plist"
    cp ${./probe/glue.c} "$out/glue.c"
    cp ${./probe/probe.c} "$out/probe.c"
    cp ${./probe/sf_rtkitprobe.h} "$out/sf_rtkitprobe.h"
    cp ${./probe/SFRtkitProbe.cpp} "$out/SFRtkitProbe.cpp"
    cp ${./probe/sf_dcp_dart.cpp} "$out/sf_dcp_dart.cpp"
    cp ${./probe/sf_dcp_dart.h} "$out/sf_dcp_dart.h"
    cp -R ${./src} "$out/rtkit"
    mkdir -p "$out/dcpdart"
    cp -R ${../m1-dcp-dart/include} "$out/dcpdart/include"
    cp -R ${../m1-dcp-dart/src} "$out/dcpdart/src"
    cp ${../m1-gpu-dt/kernel/sf_of_iter.c} "$out/dcpdart/sf_of_iter.c"
  '';
  bindgenDefines = pkgs.writeText "bindgen-defines"
    (pkgs.lib.concatMapStrings (d: "-D${d}\n") lkpi.cflags.defines);
in lkpi.drmKext.overrideAttrs (old: {
  pname = "siliconforest-rtkit-probe";
  src = pkgs.runCommand "siliconforest-rtkit-probe-tree" {} ''
    cp -R ${lkpi.tree} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/SFRtkitProbe"
    cp -R ${copied} "$out/src/Kernel/Extensions/SFRtkitProbe"
    printf '\nadd_subdirectory(SFRtkitProbe)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix -t commands SFRtkitProbe.kext > build-commands.txt

    # the layout check: -D set rtkit.c is compiled with vs what bindgen was given
    grep -m1 'rtkit/rtkit\.c\.o -c' build-commands.txt | tr ' ' '\n' | grep '^-D' \
      | grep -v -e KBUILD_MODNAME -e LINUXKPI_PARAM_PREFIX | sed 's/\\"/"/g' | sort -u > defines.rtkit
    sort -u ${bindgenDefines} > defines.bindgen
    test -s defines.rtkit
    if ! diff defines.rtkit defines.bindgen; then
      echo "rtkit.c in the kext and the Rust bindings would be built with different defines" >&2
      exit 1
    fi
    echo "defines match: $(wc -l < defines.rtkit) -D flags"

    ninja -C build-nix SFRtkitProbe.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/SFRtkitProbe/SFRtkitProbe.kext
    test -f "$bundle/Contents/MacOS/SFRtkitProbe"
    mkdir -p "$out/System/Library/Extensions" "$out/nix-support/objects"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    cp build-commands.txt "$out/nix-support/build-commands"
    for o in $(find build-nix/src/Kernel/Extensions/SFRtkitProbe -name '*.o' \
               | grep -e '/rtkit/' -e '/glue\.c' -e '/probe\.c' -e 'SFRtkitProbe\.cpp' -e 'sf_dcp_dart\.cpp' -e '/dcpdart/'); do
      cp "$o" "$out/nix-support/objects/$(basename "$o")"
    done
    ls "$out/nix-support/objects"
    ${crossToolchain}/bin/arm64-apple-darwin20.4-nm -u "$bundle/Contents/MacOS/SFRtkitProbe" \
      | sed 's/^ *U //' | sort -u > "$out/nix-support/undefined"
    echo "kext imports $(wc -l < $out/nix-support/undefined) symbols"
    runHook postInstall
  '';
})
