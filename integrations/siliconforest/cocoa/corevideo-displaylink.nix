# SPDX-License-Identifier: MIT
# CoreVideo as CODEX installs it (../finch/CoreVideo/sf-displaylink.nix: Finch's buffers and formats, SiliconForest's
# CVDisplayLink.m) with TERM9's 0149: the display link ticks on the compositor's clock (libWindowServer 0148w), and
# TERM11's 0172: at every refresh on the display's phase (it waited for a pending frame's flip and could lock to half rate).
let
  a = import ./arm64.nix;
  base = import ../finch/CoreVideo/sf-displaylink.nix;
  link = a.pkgs.runCommand "corevideo-displaylink-src" { } ''
    mkdir -p $out/src/Frameworks/CoreVideo
    cp ${a.src}/src/Frameworks/CoreVideo/CVDisplayLink.m $out/src/Frameworks/CoreVideo/
    chmod -R u+w $out
    cd $out && patch -p1 < ${./patches/0149-corevideo-display-link-on-the-compositor-clock.patch}
    patch -p1 < ${./patches/0172-corevideo-display-link-ticks-every-refresh.patch}
  '';
in base.overrideAttrs (old: {
  name = "siliconforest-corevideo-displaylink-clock-arm64";
  buildPhase = builtins.replaceStrings [ "-c ${a.src}/src/Frameworks/CoreVideo/CVDisplayLink.m" ]
    [ "-c ${link}/src/Frameworks/CoreVideo/CVDisplayLink.m" ] old.buildPhase;
})
