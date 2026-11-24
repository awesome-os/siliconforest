# Original Darwin Lab code. License selection pending.
#
# ARM64 Darwin build of userspace/memory_probe_smoke.c with the pinned
# SiliconForest cross toolchain, in-tree open-source SDK and prebuilt arm64
# libSystem. Interface (agreed with staging):
#
#   callPackage ./memory-probe-smoke.nix {
#     darwinCrossToolchain = arm64CrossToolchain;  # arm64-apple-darwin20.4-*
#     libSystem = libSystemArm64Build;
#     inherit nativeLd appleSdk;
#     src = <lab root containing userspace/ and probe/>;
#   }
#
# Produces a guest executable only. Building it is NOT a kernel, provider or
# device result; running it in a VM is a separate, explicitly authorized step.
{ stdenv
, lib
, darwinCrossToolchain
, nativeLd
, libSystem
, appleSdk
, src
}:

let
  triple = "arm64-apple-darwin20.4";
  cc = "${darwinCrossToolchain}/bin/${triple}-clang";
  otool = "${darwinCrossToolchain}/bin/${triple}-otool";
  sdkRoot = "${appleSdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
in
stdenv.mkDerivation {
  pname = "darwin-lab-memory-probe-smoke";
  version = "0.1";
  inherit src;

  dontConfigure = true;

  buildPhase = ''
    runHook preBuild
    export DARWIN_SDK_ROOT="${sdkRoot}"

    for f in userspace/memory_probe_smoke.c userspace/memory_probe_result.h \
             probe/memory_probe_ioctl.h probe/memory_probe_protocol.h; do
      [ -f "$f" ] || { echo "missing lab source: $f" >&2; exit 1; }
    done

    # Same warning policy as the host build. The SDK and libSystem headers are
    # system headers; only lab code is held to -Werror. The protocol header's
    # _Static_assert pins the current 32/40/88/48-byte message layout.
    ${cc} -isysroot "$DARWIN_SDK_ROOT" -mmacosx-version-min=26.5 \
      -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 \
      -isystem ${libSystem}/usr/include \
      -Iprobe \
      -c userspace/memory_probe_smoke.c -o memory_probe_smoke.o

    # libSystem.B.dylib re-exports libdyld; point ld at the arch-matched one
    # built with this libSystem, as cmake/prebuilt_libsystem.cmake does.
    ${cc} -isysroot "$DARWIN_SDK_ROOT" -mmacosx-version-min=26.5 \
      -fuse-ld=${nativeLd}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -Wl,-L,${libSystem}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      memory_probe_smoke.o -lSystem \
      -o memory-probe-smoke

    # Fail closed unless the output is an arm64 Mach-O executable whose only
    # library dependency is the guest libSystem.
    ${otool} -hv memory-probe-smoke | tee otool-header.txt
    grep -q 'ARM64' otool-header.txt
    grep -q 'EXECUTE' otool-header.txt
    ${otool} -L memory-probe-smoke | tee otool-libs.txt
    deps=$(tail -n +2 otool-libs.txt | awk '{print $1}')
    [ "$deps" = /usr/lib/libSystem.B.dylib ] || {
      echo "unexpected dynamic dependencies: $deps" >&2; exit 1; }
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/usr/bin
    cp memory-probe-smoke $out/usr/bin/memory-probe-smoke
    runHook postInstall
  '';

  # Mach-O guest binary: no Linux ELF fixup, patchelf or strip.
  dontFixup = true;
  dontStrip = true;

  meta = with lib; {
    description = "Darwin Lab RAM-probe smoke exerciser (/usr/bin/memory-probe-smoke), arm64 guest build";
    platforms = platforms.unix;
  };
}
