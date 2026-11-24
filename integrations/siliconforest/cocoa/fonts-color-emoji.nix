# SPDX-License-Identifier: MIT
# TERM9 (FX-024): Noto Color Emoji, COLRv1 (googlefonts/noto-emoji v2.047, unmodified, SIL Open Font License 1.1, the
# licence beside it). Its emoji glyphs have no outlines of their own (the COLR paint graph draws them): Firefox paints
# COLRv1 itself, Onyx2D does not yet, so this is for Firefox's own font directory (with font.name-list.emoji), not for
# /System/Library/Fonts, until Onyx2D draws colour glyphs (Terminal would show nothing where it shows NotoEmoji now).
let
  a = import ./arm64.nix;
  root = "https://raw.githubusercontent.com/googlefonts/noto-emoji/v2.047";
  font = a.pkgs.fetchurl { url = "${root}/fonts/Noto-COLRv1.ttf"; sha256 = "0i4vhfz6n7pcrjy7wiljk12hzzsj8v28snq29k5iyx5dnllrym13"; };
  license = a.pkgs.fetchurl { url = "${root}/fonts/LICENSE"; sha256 = "1wibzqzcb77cszr93w8bg1d4yxzgb056pkz7q1c43rrd3iagjwva"; };
in a.pkgs.runCommand "siliconforest-fonts-color-emoji" { } ''
  mkdir -p $out/NotoColorEmoji
  cp ${font} $out/NotoColorEmoji/Noto-COLRv1.ttf
  cp ${license} $out/NotoColorEmoji/OFL.txt
''
