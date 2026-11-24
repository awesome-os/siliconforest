# SPDX-License-Identifier: MIT
# Private process font configuration, including one pinned OFL fallback font.
{ prefix ? "/private/var/db/sffx/fonts-v3", thaiFallback ? false }:
let
 a=import ../cocoa/arm64.nix;
 fonts=import ./noto-fonts.nix {pkgs=a.pkgs;};
in a.pkgs.runCommand "sffx-private-font-configuration" {} ''
 mkdir -p $out/cache
 cp ${fonts.font} $out/NotoSansDevanagari-Regular.ttf
 ${a.pkgs.lib.optionalString thaiFallback ''cp ${fonts.thai} $out/NotoSansThai-Regular.ttf''}
 cp ${fonts.license} $out/OFL.txt
 cat > $out/fonts.conf <<'EOF'
 <?xml version="1.0"?>
 <!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
 <fontconfig>
  <dir>/usr/share/fonts</dir>
  <dir>/System/Library/Fonts</dir>
  <dir>${prefix}</dir>
  <cachedir>${prefix}/cache</cachedir>
  <!-- This isolated configuration has no system conf.d aliases. Keep serif
       requests serif even when the requested macOS/DejaVu family is absent. -->
  <alias binding="strong"><family>serif</family><prefer><family>Nimbus Roman</family></prefer></alias>
  <alias binding="strong"><family>Georgia</family><prefer><family>Nimbus Roman</family></prefer></alias>
  <alias binding="strong"><family>Times</family><prefer><family>Nimbus Roman</family></prefer></alias>
  <alias binding="strong"><family>DejaVu Serif</family><prefer><family>Nimbus Roman</family></prefer></alias>
  <alias binding="strong"><family>DejaVuSerif</family><prefer><family>Nimbus Roman</family></prefer></alias>
 </fontconfig>
 EOF
''
