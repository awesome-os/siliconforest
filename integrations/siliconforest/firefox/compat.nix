# SPDX-License-Identifier: MIT
# Only providers missing from the first real-M1 Firefox scan. No core, AppKit,
# OpenGL, media decoder, font or system file is selected for replacement.
{ legacyAVFoundation ? false }:
let
  a = import ../cocoa/arm64.nix;
  patches = import ../cocoa/patched.nix;
  small = a.appFrameworks.override (old: {
    srcs = old.srcs // { AuthenticationServices = patches.authenticationServicesSrc; };
  });
  own = import ../cocoa/build-own-frameworks.nix;
  names = [ "Accessibility" "AddressBook" "AuthenticationServices" "CoreMIDI"
    "IOBluetooth" "MediaPlayer" "ScreenCaptureKit" "ServiceManagement" "Vision" ]
    ++ a.pkgs.lib.optional legacyAVFoundation "AVFoundation";
in a.pkgs.runCommand "siliconforest-firefox-missing-providers" {} ''
  F=System/Library/Frameworks
  mkdir -p $out/$F $out/usr/lib
  for name in ${builtins.concatStringsSep " " names}; do
    cp -a ${small}/$F/$name.framework $out/$F/
  done
  cp -a ${own}/$F/MetalKit.framework $out/$F/
  cp -L ${small}/usr/lib/libcups.2.dylib $out/usr/lib/
  echo 'Private Firefox prefix; canonical files must not be replaced from this tree.' > $out/SCOPE.txt
  ${a.pkgs.lib.optionalString legacyAVFoundation ''
    echo 'Diagnostic only: older AVFoundation supplies speech-unavailable and BGRA display classes.' >> $out/SCOPE.txt
    echo 'It does not test the installed AVFoundation assets API. VT/CoreMedia/CoreVideo stay installed.' >> $out/SCOPE.txt
  ''}
''
