# SPDX-License-Identifier: MIT
# The "Homebrew compatibility" system overlay: every file SiliconForest's system root
# needs on top of today's image for macOS-built programs, as one tree rooted at /.
# hb_qemu.py --overlay <this output> writes it onto a copy of the QEMU system image.
#
#   usr/lib/libSystem.B.dylib          ./build-libsystem.nix (patches/libsystem-homebrew.patch)
#   usr/lib/libhbcrash.dylib           ./build-guest-tools.nix (diagnostic, not part of the system)
#   usr/lib/lib{ncurses.5.4,panel.5.4,expat.1,ffi,sqlite3,xml2.2,util}.dylib   ./build-syslibs.nix
#   usr/lib/libbz2.1.0.dylib           ./build-syslibs.nix (upstream bzip2 sources)
#   usr/lib/libedit.3.dylib            ./build-libedit.nix (upstream libedit)
#   usr/lib/libIOKitCF.dylib           ./build-iokit.nix (the tree's IOKit + kIOMainPortDefault)
#   usr/lib/libz.1.dylib, libcurl.4.dylib                    the tree's own dylib packages
#   usr/bin/curl                       the tree's curl (macOS has one here; `brew` calls it)
#   usr/share/locale                   the tree's locale data (nix/pkgs/base/locale-data.nix)
#   System/Library/Frameworks/{CoreServices,Security,SystemConfiguration}.framework
#                                      the tree's arm64 frameworks, binaries only; Security from
#                                      ./build-security.nix (+ SecTrustCopyCertificateChain),
#                                      CoreServices from ../macos-compat/build-coreservices.nix
#                                      (+ the Text Encoding Converter and LocaleRefGetPartString)
#   System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
#                                      a link to the image's /usr/lib/libFoundation.dylib, as its
#                                      Versions/A is (macOS's Foundation is version C). The tree's
#                                      framework-compat package has a binary for this path, but it
#                                      re-exports the Swift Foundation overlay, which the image lacks.
# Not copied: headers, configd and the launch daemons of the SystemConfiguration package
# (they would change how the guest configures its network).
let
  # the SiliconForest source: ../qemu-dev/sf-userland.nix (the tip with the one tree patch its
  # Security package needs to build; SF_PD=pin: the old pin e7aa00a6, the derivations of before)
  pd = import ../qemu-dev/sf-userland.nix;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  p = pd.packages.x86_64-linux;
  libSystem = import ./build-libsystem.nix;
  syslibs = import ./build-syslibs.nix;
  tools = import ./build-guest-tools.nix;
  libedit = import ./build-libedit.nix;
  iokit = import ./build-iokit.nix;
  security = import ./build-security.nix;
  # the tree's CoreServices + the text-encoding calls vim and neovim import (GitHub issue 45)
  coreServices = import ../macos-compat/build-coreservices.nix;
  # The tree's /usr/share/locale (Apple libc format: en_US.UTF-8 and C.UTF-8 among them);
  # upstream's full images carry it, the minimal image does not. Without it setlocale()
  # refuses every UTF-8 locale and tmux, among others, will not start.
  localeData = pkgs.callPackage (pd.outPath + "/nix/pkgs/base/locale-data.nix") { };
  F = "System/Library/Frameworks";
in pkgs.runCommand "darwin-lab-homebrew-overlay-arm64" { } ''
  mkdir -p $out/usr/lib $out/${F}
  cp ${libSystem}/usr/lib/libSystem.B.dylib $out/usr/lib/
  cp ${tools}/usr/lib/libhbcrash.dylib $out/usr/lib/
  mkdir -p $out/usr/bin
  cp ${tools}/usr/bin/hb-runas $out/usr/bin/
  cp -P ${syslibs}/usr/lib/* $out/usr/lib/
  cp -P ${libedit}/usr/lib/* $out/usr/lib/
  cp ${iokit}/usr/lib/libIOKitCF.dylib $out/usr/lib/
  cp -P ${p.libzDylib-arm64}/usr/lib/libz* $out/usr/lib/
  cp ${p.curl-arm64}/bin/curl $out/usr/bin/curl
  mkdir -p $out/usr/share
  cp -r ${localeData}/usr/share/locale $out/usr/share/locale
  cp -P ${p.libcurlDylib-arm64}/usr/lib/libcurl* $out/usr/lib/
  cp ${security}/usr/lib/libSecurity.dylib $out/usr/lib/
  cp ${p.systemConfiguration-arm64}/usr/lib/libSystemConfiguration.dylib $out/usr/lib/
  framework() {   # package, name: Versions/A/<name> and the two links, no headers
    mkdir -p $out/${F}/$2.framework/Versions/A
    cp -L $1/${F}/$2.framework/Versions/A/$2 $out/${F}/$2.framework/Versions/A/$2
    ln -s A $out/${F}/$2.framework/Versions/Current
    ln -s Versions/Current/$2 $out/${F}/$2.framework/$2
  }
  framework ${coreServices} CoreServices
  framework ${security} Security
  framework ${p.systemConfiguration-arm64} SystemConfiguration
  mkdir -p $out/${F}/Foundation.framework/Versions/C
  ln -s ../../../../../../usr/lib/libFoundation.dylib $out/${F}/Foundation.framework/Versions/C/Foundation
  chmod -R u+w $out
''
