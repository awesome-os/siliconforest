# Exact upstream CMake target, linked to retained libSystem9fc; no daemon patch.
let
  pd = (import ../rebase/pd.nix).pd;
  system = import ../m1-userspace-diagnostics/build-libsystem-spawnattr.nix;
  selected = pd.packages.x86_64-linux.userland-arm64.override {
    pname = "puredarwin-native-mdnsd-9fc";
    buildTargets = [ "mdnsd" ];
    prebuiltLibSystem = system;
    xorgDriverIncludes = null;
    installUserlandTargetsOnly = true;
  };
in selected.overrideAttrs (old: {
  installPhase = ''
    mkdir -p "$out/usr/sbin" "$out/share/native-resolver-build/source/src/Libraries" \
      "$out/share/native-resolver-build/source/src/Userspace" "$out/share/native-resolver-build/source/cmake"
    cp build-nix/src/Userspace/mdnsd/mdnsd "$out/usr/sbin/"
    cp build-nix/CMakeCache.txt build-nix/build.ninja "$out/share/native-resolver-build/"
    ninja -C build-nix -t commands mdnsd > "$out/share/native-resolver-build/mdnsd-commands.txt"
    ninja -C build-nix -t deps > "$out/share/native-resolver-build/compiler-dependencies.txt"
    cp -R src/Libraries/mDNSResponder "$out/share/native-resolver-build/source/src/Libraries/"
    cp -R src/Userspace/mdnsd "$out/share/native-resolver-build/source/src/Userspace/"
    cp cmake/crosscompile.cmake cmake/prebuilt_libsystem.cmake "$out/share/native-resolver-build/source/cmake/"
    cp CMakeLists.txt "$out/share/native-resolver-build/source/"
  '';
  dontFixup = true;
  dontStrip = true;
})
