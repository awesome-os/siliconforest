# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  ssl = import ../corecrypto/openssl.nix;
  retainedSecurity = import ../../homebrew/build-security.nix;
  symptom = p.symptomReporter.override {
    siliconforestArch = "arm64";
    arm64CrossToolchain = p.arm64-cross-toolchain;
  };
  sc = p.systemConfiguration-arm64.override {
    extraCmakeFlags = [
      "-DSILICONFOREST_ENABLE_SYSTEMCONFIGURATION=ON"
      "-DSILICONFOREST_COREFOUNDATION_PREFIX=${p.coreFoundation-arm64}"
      "-DSILICONFOREST_LIBOBJC_PREFIX=${p.libobjc-arm64}"
      "-DSILICONFOREST_SECURITY_PREFIX=${retainedSecurity}"
      "-DSILICONFOREST_IOKIT_PREFIX=${p.iokit-arm64}"
      "-DSILICONFOREST_SYMPTOMREPORTER_PREFIX=${symptom}"
    ];
  };
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-CFNetwork-arm64";
  src = finch;
  patches = [ ./patches/0001-sf-runtime-layout.patch ./patches/0002-sf-http-stream-client.patch ];
  nativeBuildInputs = [ pkgs.python3 ];
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    cp -r ${./include} headers
    chmod -R u+w headers
    cp ${e.pd.outPath}/src/Frameworks/CFNetwork/CFProxySupport.h headers/CFNetwork/
    mkdir objects
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -D_DARWIN_C_SOURCE -I$PWD/headers -I${libsystem}/usr/include -I${p.coreFoundation-arm64}/include -I${sc}/include -I${ssl}/include -I${e.pd.outPath}/src/Frameworks -I${e.pd.outPath}/src/Libraries/mDNSResponder/mDNSShared"
    for s in userland/CFNetwork/*.c; do
      ${cc} $CF -DSF_CFNETWORK_PREPROCESS -E -P "$s" -o objects/$(basename "$s" .c).i
    done
    python3 ${./declarations.py} ${../tools/c_functions.py} userland/CFNetwork headers/CFNetwork/Functions.h objects/*.i
    for s in userland/CFNetwork/*.c; do
      ${cc} $CF -c "$s" -o objects/$(basename "$s" .c).o
    done
    # Expose CFNetwork's own API, hiding the statically linked TLS implementation.
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm -g -U objects/*.o | sed -n 's/.* [TDS] \(_\(CF\|_CF\|kCF\)[^ ]*\)$/\1/p' | sort -u > exports
    ${cc} -dynamiclib objects/*.o --ld-path=${ld} -nostdlib -isysroot ${sdk} \
      -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.coreFoundation-arm64}/usr/lib -L${p.foundation-arm64}/usr/lib -F${sc}/System/Library/Frameworks \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-exported_symbols_list,exports \
      -install_name /System/Library/Frameworks/CFNetwork.framework/Versions/A/CFNetwork -current_version 3860.255.112 -compatibility_version 1 \
      ${ssl}/lib/libssl.a ${ssl}/lib/libcrypto.a -framework SystemConfiguration -Wl,-reexport_library,${p.foundation-arm64}/usr/lib/libFoundation.dylib -lCoreFoundation -lFoundation -lSystem -o CFNetwork
  '';
  installPhase = ''
    fw=$out/System/Library/Frameworks/CFNetwork.framework
    mkdir -p $fw/Versions/A/Resources/Licenses
    cp CFNetwork $fw/Versions/A/
    cp -r headers/CFNetwork $fw/Versions/A/Headers
    ln -s A $fw/Versions/Current
    ln -s Versions/Current/CFNetwork $fw/CFNetwork
    ln -s Versions/Current/Headers $fw/Headers
    cat > $fw/Versions/A/Resources/Info.plist <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>com.apple.CFNetwork</string><key>CFBundleExecutable</key><string>CFNetwork</string><key>CFBundleVersion</key><string>3860.500.112</string></dict></plist>
PLIST
    cp ${finch}/LICENSE* $fw/Versions/A/Resources/Licenses/
    tar -xOf ${ssl.src} openssl-${ssl.version}/LICENSE.txt > $fw/Versions/A/Resources/Licenses/OpenSSL-LICENSE.txt
  '';
  dontFixup = true;
  dontStrip = true;
}
