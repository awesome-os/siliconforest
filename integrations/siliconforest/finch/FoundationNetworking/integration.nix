# SPDX-License-Identifier: MIT
# Concrete proposal for TERM6's guarded Foundation pipeline. Never installed here.
# Pass TERM6's latest reviewed Foundation derivation, preserving its other patches.
{ foundation }:
let
 e=import ../environment.nix;
 inherit(e) pkgs;
 net=import ./canonical-objects.nix;
 ssl=import ../corecrypto/openssl.nix;
 cfn=import ../CFNetwork/build.nix;
 marker="-o libFoundation.dylib $objs";
 extra="-Wl,-force_load,${net}/lib/libFinchNetworking.a ${ssl}/lib/libssl.a ${ssl}/lib/libcrypto.a -F${cfn}/System/Library/Frameworks -framework CFNetwork";
in foundation.overrideAttrs(old: assert pkgs.lib.hasInfix marker old.buildPhase; {
 postPatch=(old.postPatch or "")+''
  # Remove old class implementations, keeping all other Foundation sources.
  for unit in NSURLRequest NSURLSession NSURLProtocol NSURLAuthenticationChallenge; do
   test -f URL.subproj/$unit.m
   printf '%s\n' '/* Networking implementation supplied by libFinchNetworking.a. */' > URL.subproj/$unit.m
  done
 '';
 buildPhase=pkgs.lib.replaceStrings [marker] ["-o libFoundation.dylib $objs ${extra}"] old.buildPhase;
 postInstall=(old.postInstall or "")+''
  resources=$out/System/Library/Frameworks/Foundation.framework/Versions/A/Resources
  mkdir -p $resources/Licenses/FinchNetworking
  cp ${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt $resources/
  cp ${e.finch}/LICENSE* $resources/Licenses/FinchNetworking/
  cp ${./CA-NOTICE} $resources/Licenses/FinchNetworking/CA-NOTICE
  tar -xOf ${ssl.src} openssl-${ssl.version}/LICENSE.txt > $resources/Licenses/FinchNetworking/OpenSSL-LICENSE.txt
 '';
})
