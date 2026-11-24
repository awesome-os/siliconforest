# SPDX-License-Identifier: MIT
# Objective-C model/store/context implementation, including SQLite. No Swift overlay.
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  sqlite = p.sqlite-arm64;
  sqlitePrivate = pkgs.writeText "coredata-sqlite-private.exports" "_sqlite3*\n";
  foundation = import ../../terminal-app/build-foundation.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-CoreData-objc-sqlite-arm64";
  src = finch;
  patches = [ ./patches/0001-sf-array-comparator-api.patch ];
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir objects
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -fno-objc-arc -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined -Wno-incomplete-implementation -I${./include} -I${foundation}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include -I${sqlite}/include -I${e.pd.outPath}/src/Frameworks"
    for name in CDConstants CoreDataConstants_Generated CDModel CDRequest CDStore CDCustomStore CDMigration CDNewClasses CDXMLStore CDBinaryStore CDContext CDContainer CDFetchedResultsController CDExtensions; do
      ${cc} $CF -c userland/CoreData/$name.m -o objects/$name.o
    done
    ${cc} -dynamiclib objects/*.o --ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${foundation}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -L${sqlite}/lib -lsqlite3 -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -Wl,-unexported_symbols_list,${sqlitePrivate} -install_name /System/Library/Frameworks/CoreData.framework/Versions/A/CoreData -current_version 1526 -compatibility_version 1 -o CoreData
  '';
  installPhase = ''
    fw=$out/System/Library/Frameworks/CoreData.framework
    mkdir -p $fw/Versions/A/Resources/Licenses
    cp CoreData $fw/Versions/A/
    cp -r ${./include}/CoreData $fw/Versions/A/Headers
    ln -s A $fw/Versions/Current
    ln -s Versions/Current/CoreData $fw/CoreData
    ln -s Versions/Current/Headers $fw/Headers
    cp LICENSE* $fw/Versions/A/Resources/Licenses/
  '';
  dontFixup = true;
  dontStrip = true;
}
