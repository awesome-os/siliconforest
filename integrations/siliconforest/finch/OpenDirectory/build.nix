# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  prefix = "/System/Library/Frameworks/OpenDirectory.framework/Versions/A";
  cfdir = "${prefix}/Frameworks/CFOpenDirectory.framework/Versions/A";
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-OpenDirectory-arm64";
  src = finch;
  patches = [ ./patches/0001-accept-contains-match.patch ];
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir objects
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -D_DARWIN_C_SOURCE -Wno-deprecated-declarations -Wno-nullability-completeness -I${./include} -Iuserland/account-tools -I${e.pd.outPath}/src/Libraries/CommonCrypto/include -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include"
    LF="--ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5"
    for s in account_store password_hash; do
      ${cc} $CF -c userland/account-tools/$s.c -o objects/$s.o
    done
    ${cc} $CF -dynamiclib objects/account_store.o objects/password_hash.o $LF -install_name ${prefix}/Libraries/libfinchaccount.dylib -lSystem -o libfinchaccount.dylib
    ${cc} $CF -c userland/OpenDirectory/CFOpenDirectory.c -o objects/cf.o
    ${cc} $CF -fno-objc-arc -c userland/OpenDirectory/CFOpenDirectoryConstants_Generated.m -o objects/cfconst.o
    ${cc} $CF -dynamiclib objects/cf.o objects/cfconst.o $LF -L. -lfinchaccount -lCoreFoundation -lFoundation -lobjc -lSystem -install_name ${cfdir}/CFOpenDirectory -current_version 1 -compatibility_version 1 -o CFOpenDirectory
    ${cc} $CF -fno-objc-arc -Wno-incomplete-implementation -c userland/OpenDirectory/ODObjects.m -o objects/od.o
    ${cc} $CF -fno-objc-arc -c userland/OpenDirectory/OpenDirectoryConstants_Generated.m -o objects/odconst.o
    ${cc} $CF -dynamiclib objects/od.o objects/odconst.o $LF -L. -lfinchaccount -Wl,-reexport_library,$PWD/CFOpenDirectory -lFoundation -lCoreFoundation -lobjc -lSystem -install_name ${prefix}/OpenDirectory -current_version 1 -compatibility_version 1 -o OpenDirectory
  '';
  installPhase = ''
    od=$out${prefix}; cf=$out${cfdir}
    mkdir -p $od/Resources/Licenses $cf/Resources/Licenses $od/Libraries
    cp OpenDirectory $od/; cp CFOpenDirectory $cf/; cp libfinchaccount.dylib $od/Libraries/
    for fw in $od $cf; do
      n=$(basename $(dirname $(dirname $fw)))
      n=''${n%.framework}
      ln -s A $(dirname $fw)/Current
      ln -s Versions/Current/$n $(dirname $(dirname $fw))/$n
      cp ${finch}/LICENSE* $fw/Resources/Licenses/
    done
    cp -r ${./include}/OpenDirectory $od/Headers
    cp -r ${./include}/CFOpenDirectory $cf/Headers
    for fw in $od $cf; do
      name=$(basename $(dirname $(dirname $fw))); name=''${name%.framework}
      ln -s Versions/Current/Headers $(dirname $(dirname $fw))/Headers
      cat > $fw/Resources/Info.plist <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>com.apple.$name</string><key>CFBundleExecutable</key><string>$name</string><key>CFBundlePackageType</key><string>FMWK</string><key>CFBundleVersion</key><string>1</string></dict></plist>
PLIST
    done
    ln -s Versions/Current/Frameworks $out/System/Library/Frameworks/OpenDirectory.framework/Frameworks
  '';
  dontFixup = true;
  dontStrip = true;
}
