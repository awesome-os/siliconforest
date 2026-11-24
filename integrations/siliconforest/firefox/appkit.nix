# SPDX-License-Identifier: MIT
# TERM7 adopted the proven0201/0203/0204 patches into the main series.
# Follow that assembled provider, retaining subsequent Cocoa-owner fixes.
(import ../cocoa/patched.nix).appkit.overrideAttrs (old: {
 postInstall = (old.postInstall or "") + ''
  # Mozilla imports AppKit's public C version; SiliconForest builds its A ABI.
  ln -sfn A $out/System/Library/Frameworks/AppKit.framework/Versions/C
 '';
})
