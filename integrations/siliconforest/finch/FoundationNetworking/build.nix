# SPDX-License-Identifier: MIT
# Prefix-only networking classes; Foundation itself is never replaced.
let
 e=import ../environment.nix;
 inherit(e) pkgs p cc ld libsystem sdk finch;
 ssl=import ../corecrypto/openssl.nix;
 cfn=import ../CFNetwork/build.nix;
in pkgs.stdenv.mkDerivation {
 name="siliconforest-finch-foundation-networking-prefix-arm64";
 src=finch;
 nativeBuildInputs=[pkgs.python3];
 dontConfigure=true;
 buildPhase=''
  export DARWIN_SDK_ROOT=${sdk}
  mkdir -p net objects headers
  cp -r ${cfn}/System/Library/Frameworks/CFNetwork.framework/Versions/A/Headers headers/CFNetwork
  cp userland/Foundation/NSURLSession{,Sockets}.m userland/Foundation/NSURLSession_Finch.h userland/Foundation/NSURLCache.m userland/Foundation/NSHTTPCookie.m userland/Foundation/NSURLCredential.m net/
  cp ${./sf-tls.h} net/sf-tls.h
  cp ${./sf-http-date.h} net/sf-http-date.h
  python3 ${./port.py} net
  substituteInPlace net/NSURLCredential.m --replace-fail '#import <Security/Security.h>' '/* Opaque trust/identity declarations supplied by our prefix header. */'
  CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -fno-objc-arc -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined -I$PWD/headers -I${./include} -I${ssl}/include -I${../CFNetwork/include} -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include -I${e.pd.outPath}/src/Frameworks -include ${./include}/SFNetworking.h"
  for name in NSURLSession NSURLSessionSockets NSURLCache NSHTTPCookie NSURLCredential; do
   ${cc} $CF -c net/$name.m -o objects/$name.o
  done
  ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm -g -U objects/*.o | sed -n 's/.* [TDS] \(_OBJC_.*SFNS[^ ]*\)$/\1/p' | sort -u > exports
  ${cc} -dynamiclib objects/*.o --ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -F${cfn}/System/Library/Frameworks -framework CFNetwork -lFoundation -lCoreFoundation -lobjc -lSystem ${ssl}/lib/libssl.a ${ssl}/lib/libcrypto.a -Wl,-exported_symbols_list,exports -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -install_name @rpath/libSFNetworking.dylib -o libSFNetworking.dylib
 '';
 installPhase=''
  mkdir -p $out/lib $out/include $out/share/certs $out/share/licenses/Finch
  cp libSFNetworking.dylib $out/lib/
  cp ${./include}/SFNetworking.h $out/include/
  cp ${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt $out/share/certs/
  cp LICENSE* $out/share/licenses/Finch/
  mkdir -p $out/share/licenses/OpenSSL
  tar -xOf ${ssl.src} openssl-${ssl.version}/LICENSE.txt > $out/share/licenses/OpenSSL/LICENSE.txt
  cp ${./CA-NOTICE} $out/share/certs/NOTICE
 '';
 dontFixup=true;dontStrip=true;
}
