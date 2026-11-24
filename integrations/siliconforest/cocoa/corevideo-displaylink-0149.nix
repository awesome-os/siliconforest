# SPDX-License-Identifier: MIT
# CoreVideo as the M1 carries it system-wide (read from the machine on 17 November 2026: its file is this recipe's
# build): ./corevideo-displaylink.nix without TERM11's 0172, that is Finch's buffers and formats, the lab's
# CVDisplayLink.m, and TERM9's 0149 (the display link ticks on the compositor's clock). 0172 is selected only inside
# Firefox's private video family (../finch/firefox-media/clocked-corevideo.nix takes ./corevideo-displaylink.nix).
# Kept as its own recipe so that the renamed system-wide file is the installed variant and not a newer one.
let
  a = import ./arm64.nix;
  base = import ../finch/CoreVideo/sf-displaylink.nix;
  link = a.pkgs.runCommand "corevideo-displaylink-src" { } ''
    mkdir -p $out/src/Frameworks/CoreVideo
    cp ${a.src}/src/Frameworks/CoreVideo/CVDisplayLink.m $out/src/Frameworks/CoreVideo/
    chmod -R u+w $out
    cd $out && patch -p1 < ${./patches/0149-corevideo-display-link-on-the-compositor-clock.patch}
  '';
in base.overrideAttrs (old: {
  name = "siliconforest-corevideo-displaylink-clock-arm64";
  buildPhase = builtins.replaceStrings [ "-c ${a.src}/src/Frameworks/CoreVideo/CVDisplayLink.m" ]
    [ "-c ${link}/src/Frameworks/CoreVideo/CVDisplayLink.m" ] old.buildPhase;
})
