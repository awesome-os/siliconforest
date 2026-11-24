# SPDX-License-Identifier: MIT
# Small whole Finch frameworks, against open SiliconForest headers and libraries.
{ name, version ? "1", arc ? true, security ? null, patches ? [],
  extraIncludes ? "", extraLinks ? "", prepare ? "", foundation ? null }:
let
  e = import ./environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  foundationLink = if foundation == null then p.foundation-arm64 else foundation;
  headers = ./. + "/${name}/include";
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-${name}-arm64";
  src = finch;
  inherit patches;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    ${prepare}
    mkdir objects
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions ${if arc then "-fobjc-arc" else "-fno-objc-arc"} -D_DARWIN_C_SOURCE -Wno-deprecated-declarations -Wno-availability -Wno-nullability-completeness -Wno-incomplete-implementation -I${headers} -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include ${extraIncludes}"
    for s in userland/${name}/*.m; do
      ${cc} $CF -c "$s" -o objects/$(basename "$s" .m).o
    done
    ${cc} -dynamiclib objects/*.o --ld-path=${ld} -nostdlib -isysroot ${sdk} \
      -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${foundationLink}/usr/lib \
      -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -install_name /System/Library/Frameworks/${name}.framework/Versions/A/${name} \
      -compatibility_version 1 -current_version ${version} ${pkgs.lib.optionalString (security != null) "-F${security}/System/Library/Frameworks -framework Security"} \
      -lFoundation \
      ${extraLinks} -lCoreFoundation -lobjc -lSystem -o ${name}
  '';
  installPhase = ''
    fw=$out/System/Library/Frameworks/${name}.framework
    mkdir -p $fw/Versions/A/Resources/Licenses $out/usr/share/licenses/finch-${name}
    cp ${name} $fw/Versions/A/
    cp -r ${headers}/${name} $fw/Versions/A/Headers
    ln -s A $fw/Versions/Current
    ln -s Versions/Current/${name} $fw/${name}
    ln -s Versions/Current/Headers $fw/Headers
    cp ${finch}/LICENSE* $out/usr/share/licenses/finch-${name}/
    cp ${finch}/LICENSE* $fw/Versions/A/Resources/Licenses/
    cat > $fw/Versions/A/Resources/Info.plist <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>com.apple.${name}</string>
<key>CFBundleExecutable</key><string>${name}</string>
<key>CFBundleName</key><string>${name}</string>
<key>CFBundlePackageType</key><string>FMWK</string>
<key>CFBundleVersion</key><string>${version}</string>
</dict></plist>
PLIST
  '';
  dontFixup = true;
  dontStrip = true;
}
