# SPDX-License-Identifier: MIT
# sf-jit-test for the SiliconForest arm64 guest (toolchain and link shape of ../../build-sfnvme-test.nix),
# signed three ways by ./sf_cs_sign.py (docs/roadmap/issues/SF-X-claude-code.md):
#   sf-jit-adhoc             the linker's ad-hoc signature (as a Homebrew bottle: node)
#   sf-jit-runtime-entitled  hardened runtime + the entitlements a vendor's JIT program carries
#                            (the Claude Code CLI's: allow-jit, allow-unsigned-executable-memory,
#                            disable-library-validation; plus disable-executable-page-protection)
#   sf-jit-runtime-bare      hardened runtime, no entitlements (what macOS gives none of this to)
# and sf-xpc-probe (the libxpc call that ended the Claude Code CLI; tells an old libSystem from a fixed one).
#   nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-jit-test.nix
let
  pd = (import ../../../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  libSystem = import ../../../m1-userspace-diagnostics/build-libsystem-spawnattr.nix;
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-sf-jit-test-arm64";
  dontUnpack = true;
  nativeBuildInputs = [ pkgs.python3 ];
  buildPhase = ''
    export DARWIN_SDK_ROOT=${p.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -O1 \
      -isystem "$DARWIN_SDK_ROOT/usr/include" \
      ${./sf-jit-test.c} \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${libSystem}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lSystem -o sf-jit-adhoc
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang \
      -std=c11 -Wall -Wextra -O1 \
      -isystem "$DARWIN_SDK_ROOT/usr/include" \
      ${./sf-xpc-probe.c} \
      --ld-path=${p.native-ld}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${libSystem}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lSystem -o sf-xpc-probe
    mkdir tools homebrew
    cp ${./sf_cs_sign.py} tools/sf_cs_sign.py
    cp ${../../../homebrew/hb_macho.py} homebrew/hb_macho.py
    sed -i "s|parents\[3\] / 'homebrew'|parents[1] / 'homebrew'|" tools/sf_cs_sign.py
    python3 -B tools/sf_cs_sign.py sf-jit-adhoc sf-jit-runtime-entitled --runtime \
      --entitlement com.apple.security.cs.allow-jit \
      --entitlement com.apple.security.cs.allow-unsigned-executable-memory \
      --entitlement com.apple.security.cs.disable-library-validation \
      --entitlement com.apple.security.cs.disable-executable-page-protection
    python3 -B tools/sf_cs_sign.py sf-jit-adhoc sf-jit-runtime-bare --runtime
  '';
  installPhase = ''
    mkdir -p "$out/usr/share/sf-test/jit"
    cp sf-jit-adhoc sf-jit-runtime-entitled sf-jit-runtime-bare "$out/usr/share/sf-test/jit/"
    cp sf-xpc-probe "$out/usr/share/sf-test/"
    chmod 755 "$out"/usr/share/sf-test/jit/* "$out/usr/share/sf-test/sf-xpc-probe"
  '';
  dontFixup = true;
  dontStrip = true;
}
